data "aws_caller_identity" "current" {}

# ATS endpoint — the non-ATS (VeriSign-signed) endpoint type is legacy; ATS is what AWS
# issues by default for new accounts and what the Paho client on the device side expects.
data "aws_iot_endpoint" "this" {
  endpoint_type = "iot:Data-ATS"
}

# One reusable policy *document*, not an attached aws_iot_policy resource — the
# pairing-status Lambda creates a single named IoT policy from this document on first use
# (iot:CreatePolicy is idempotent-by-name in the handler: create-if-missing) and attaches
# it to every device's certificate. The ${iot:Connection.Thing.ThingName} policy variables
# are resolved by IoT Core itself, per connection, so one shared policy scopes every device
# to only its own topics — a per-device policy would be more isolation (revoke one device
# without touching the rest) but IoT Core policy names must be unique, so it'd mean
# generating and tracking a name per device for no behavioral difference at this fleet
# size; shared is simpler and was chosen over it for that reason.
#
# $${...} (not ${...}) escapes Terraform's own interpolation syntax so the literal IoT
# policy variable reaches the JSON output unresolved.
locals {
  iot_policy_document = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = "iot:Connect"
        Resource = "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:client/$${iot:Connection.Thing.ThingName}"
      },
      {
        Effect   = "Allow"
        Action   = "iot:Publish"
        Resource = "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:topic/fleet/$${iot:Connection.Thing.ThingName}/telemetry"
      },
      {
        Effect = "Allow"
        Action = ["iot:Subscribe", "iot:Receive"]
        Resource = [
          "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:topicfilter/fleet/$${iot:Connection.Thing.ThingName}/commands",
          "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:topic/fleet/$${iot:Connection.Thing.ThingName}/commands",
        ]
      }
    ]
  })
}
