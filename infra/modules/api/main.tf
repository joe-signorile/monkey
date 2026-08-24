terraform {
  required_providers {
    archive = {
      source  = "hashicorp/archive"
      version = "~> 2.0"
    }
  }
}

data "aws_caller_identity" "current" {}

# One entry per API route. Each Lambda gets exactly the DynamoDB actions/tables and IoT
# actions it plausibly needs — nothing gets blanket table or "iot:*" access. Handler code
# is a placeholder (see lambda/<name>/index.js); a follow-up task fills in real logic
# without needing to touch this wiring.
locals {
  routes = {
    pairing-request = {
      method        = "POST"
      path          = "/pairing/request"
      authorized    = false
      dynamo        = { fleet-pairing-codes = ["dynamodb:PutItem"] }
      iot_actions   = []
      iot_resources = []
    }
    # Cert issuance lives here, not in pairing-claim: per the fleet-dashboard plan, the
    # one-time cert+key bundle is minted on the device's *next status poll after being
    # claimed*, not at claim time itself — the claim Lambda only flips `claimed`, this one
    # does the actual iot:CreateThing/CreateKeysAndCertificate/... work and deletes the
    # pairing-codes row so the bundle can never be issued twice from the same code.
    #
    # iot:CreateKeysAndCertificate / iot:CreatePolicy don't support resource-level
    # restriction at all (AWS requires Resource = "*" for both); iot:CreateThing,
    # iot:AttachPolicy, iot:AttachThingPrincipal do support it in principle but only by
    # thing/policy name, which doesn't exist yet at the point this Lambda calls them for a
    # brand-new device — "*" is the honest floor here, not a shortcut.
    pairing-status = {
      method     = "GET"
      path       = "/pairing/status"
      authorized = false
      dynamo     = { fleet-pairing-codes = ["dynamodb:GetItem", "dynamodb:DeleteItem"] }
      iot_actions = [
        "iot:CreateKeysAndCertificate",
        "iot:CreatePolicy",
        "iot:AttachPolicy",
        "iot:CreateThing",
        "iot:AttachThingPrincipal",
      ]
      iot_resources = ["*"]
    }
    # authorized = true: this is the dashboard-facing claim step (operator names a device
    # in the UI), unlike pairing-request/pairing-status which are unauthenticated
    # device-facing polls. DeleteItem is deliberately absent here — the pairing-codes row
    # is only deleted by pairing-status, at the single-use cert handoff; claim just sets
    # `claimed = true` (UpdateItem) so that poll knows to proceed.
    pairing-claim = {
      method     = "POST"
      path       = "/pairing/claim"
      authorized = true
      dynamo = {
        fleet-pairing-codes = ["dynamodb:GetItem", "dynamodb:UpdateItem"]
        fleet-devices       = ["dynamodb:PutItem"]
      }
      iot_actions   = []
      iot_resources = []
    }
    devices-list = {
      method        = "GET"
      path          = "/devices"
      authorized    = true
      dynamo        = { fleet-devices = ["dynamodb:Scan"] }
      iot_actions   = []
      iot_resources = []
    }
    device-commands = {
      method     = "POST"
      path       = "/devices/{id}/commands"
      authorized = true
      dynamo = {
        fleet-devices  = ["dynamodb:GetItem"]
        fleet-commands = ["dynamodb:PutItem"]
      }
      iot_actions   = ["iot:Publish"]
      iot_resources = ["arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:topic/fleet/*/commands"]
    }
    commands-fanout = {
      method     = "POST"
      path       = "/commands"
      authorized = true
      dynamo = {
        fleet-devices  = ["dynamodb:Scan"]
        fleet-commands = ["dynamodb:PutItem"]
      }
      iot_actions   = ["iot:Publish"]
      iot_resources = ["arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:topic/fleet/*/commands"]
    }
    devices-allowlist = {
      method        = "PUT"
      path          = "/devices/{id}/allowlist"
      authorized    = true
      dynamo        = { fleet-devices = ["dynamodb:UpdateItem"] }
      iot_actions   = []
      iot_resources = []
    }
  }
}

# HTTP API, not REST — no usage plans/models/request validators this project needs, and
# HTTP APIs are billed at roughly a third of REST API's per-request cost.
resource "aws_apigatewayv2_api" "this" {
  name          = "${var.project_name}-api"
  protocol_type = "HTTP"

  tags = {
    Project = var.project_name
  }
}

resource "aws_apigatewayv2_stage" "default" {
  api_id      = aws_apigatewayv2_api.this.id
  name        = "$default"
  auto_deploy = true

  tags = {
    Project = var.project_name
  }
}

resource "aws_apigatewayv2_authorizer" "cognito" {
  api_id           = aws_apigatewayv2_api.this.id
  authorizer_type  = "JWT"
  identity_sources = ["$request.header.Authorization"]
  name             = "${var.project_name}-cognito"

  jwt_configuration {
    audience = [var.cognito_user_pool_client_id]
    issuer   = "https://cognito-idp.${var.aws_region}.amazonaws.com/${var.cognito_user_pool_id}"
  }
}

data "archive_file" "lambda" {
  for_each = local.routes

  type        = "zip"
  source_dir  = "${path.module}/lambda/${each.key}"
  output_path = "${path.module}/lambda/${each.key}.zip"
}

data "aws_iam_policy_document" "assume_lambda" {
  statement {
    effect  = "Allow"
    actions = ["sts:AssumeRole"]

    principals {
      type        = "Service"
      identifiers = ["lambda.amazonaws.com"]
    }
  }
}

resource "aws_iam_role" "lambda" {
  for_each = local.routes

  name               = "${var.project_name}-${each.key}"
  assume_role_policy = data.aws_iam_policy_document.assume_lambda.json

  tags = {
    Project = var.project_name
  }
}

# Basic execution role covers CloudWatch Logs only — every Lambda needs that regardless
# of what else it does, so it's the one thing not worth hand-writing per route.
resource "aws_iam_role_policy_attachment" "logs" {
  for_each = local.routes

  role       = aws_iam_role.lambda[each.key].name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

data "aws_iam_policy_document" "route" {
  for_each = local.routes

  dynamic "statement" {
    for_each = each.value.dynamo
    content {
      effect    = "Allow"
      actions   = statement.value
      resources = [var.dynamo_table_arns[statement.key]]
    }
  }

  dynamic "statement" {
    for_each = length(each.value.iot_actions) > 0 ? [1] : []
    content {
      effect    = "Allow"
      actions   = each.value.iot_actions
      resources = each.value.iot_resources
    }
  }
}

resource "aws_iam_role_policy" "route" {
  for_each = local.routes

  name   = "${var.project_name}-${each.key}"
  role   = aws_iam_role.lambda[each.key].id
  policy = data.aws_iam_policy_document.route[each.key].json
}

resource "aws_lambda_function" "route" {
  for_each = local.routes

  function_name    = "${var.project_name}-${each.key}"
  role             = aws_iam_role.lambda[each.key].arn
  handler          = "index.handler"
  runtime          = "nodejs20.x"
  timeout          = 10
  filename         = data.archive_file.lambda[each.key].output_path
  source_code_hash = data.archive_file.lambda[each.key].output_base64sha256

  environment {
    variables = {
      FLEET_DEVICES_TABLE       = var.dynamo_table_names["fleet-devices"]
      FLEET_PAIRING_CODES_TABLE = var.dynamo_table_names["fleet-pairing-codes"]
      FLEET_COMMANDS_TABLE      = var.dynamo_table_names["fleet-commands"]
      IOT_ENDPOINT              = var.iot_endpoint
      IOT_POLICY_DOCUMENT       = var.iot_policy_document
    }
  }

  tags = {
    Project = var.project_name
  }
}

resource "aws_apigatewayv2_integration" "route" {
  for_each = local.routes

  api_id                 = aws_apigatewayv2_api.this.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.route[each.key].invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_route" "route" {
  for_each = local.routes

  api_id             = aws_apigatewayv2_api.this.id
  route_key          = "${each.value.method} ${each.value.path}"
  target             = "integrations/${aws_apigatewayv2_integration.route[each.key].id}"
  authorization_type = each.value.authorized ? "JWT" : "NONE"
  authorizer_id      = each.value.authorized ? aws_apigatewayv2_authorizer.cognito.id : null
}

resource "aws_lambda_permission" "apigateway" {
  for_each = local.routes

  statement_id  = "AllowAPIGatewayInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.route[each.key].function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.this.execution_arn}/*/*"
}
