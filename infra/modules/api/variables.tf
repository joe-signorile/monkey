variable "project_name" {
  type = string
}

variable "aws_region" {
  type = string
}

variable "dynamo_table_arns" {
  description = "Map of logical table name (fleet-devices, fleet-pairing-codes, fleet-commands) to ARN, from modules/dynamo."
  type        = map(string)
}

variable "dynamo_table_names" {
  description = "Same keys as dynamo_table_arns; passed to Lambdas as env vars so table names aren't hardcoded in handler code."
  type        = map(string)
}

variable "iot_endpoint" {
  description = "IoT Core data endpoint, from modules/iot; injected into every Lambda's env but only meaningful to pairing-status (returns it to the device) and the command-publishing routes."
  type        = string
}

variable "iot_policy_document" {
  description = "Reusable IoT policy JSON, from modules/iot; the pairing-status Lambda uses it verbatim to create one shared policy (see modules/iot/main.tf) attached to every device's certificate."
  type        = string
}

variable "cognito_user_pool_id" {
  type = string
}

variable "cognito_user_pool_client_id" {
  type = string
}
