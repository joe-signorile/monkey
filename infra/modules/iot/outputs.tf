output "endpoint" {
  value = data.aws_iot_endpoint.this.endpoint_address
}

output "policy_document" {
  description = "Reusable IoT policy JSON; the pairing-status Lambda creates one shared named IoT policy from this document (via iot:CreatePolicy, create-if-missing) and attaches it to every device's certificate."
  value       = local.iot_policy_document
}
