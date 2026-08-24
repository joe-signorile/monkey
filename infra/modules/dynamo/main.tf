# PAY_PER_REQUEST on all three tables: a personal fleet's traffic is spiky (a burst of
# telemetry writes, then long idle stretches) and low-volume overall, so provisioned
# capacity would mean either paying for headroom that sits idle or hand-tuning autoscaling
# for a handful of requests a minute. On-demand billing also has no free-tier-eligible
# capacity to plan against — it's the cheaper choice at this scale, not just the simpler one.

# Schema beyond the key is intentionally undeclared — DynamoDB is schemaless past the key,
# and the item shape (name, secretHash, pairedAt, lastCheckIn, batteryLevel, installedApps,
# allowList, lockState, desiredLockState) lives in the Lambda handlers, not here.
resource "aws_dynamodb_table" "fleet_devices" {
  name         = "fleet-devices"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "deviceId"

  attribute {
    name = "deviceId"
    type = "S"
  }

  tags = {
    Project = var.project_name
  }
}

# TTL on expiresAt: an unclaimed pairing code is only ever meant to live for the few
# seconds/minutes a device polls for it — DynamoDB's TTL sweep deletes stale codes for
# free, no cleanup Lambda needed.
resource "aws_dynamodb_table" "fleet_pairing_codes" {
  name         = "fleet-pairing-codes"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "pairingCode"

  attribute {
    name = "pairingCode"
    type = "S"
  }

  ttl {
    attribute_name = "expiresAt"
    enabled        = true
  }

  tags = {
    Project = var.project_name
  }
}

# This table is a delivery/audit trail, not the transport (IoT Core is) — TTL on `ttl`
# keeps it from growing unbounded since nothing else here rotates it.
resource "aws_dynamodb_table" "fleet_commands" {
  name         = "fleet-commands"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "deviceId"
  range_key    = "commandId"

  attribute {
    name = "deviceId"
    type = "S"
  }

  attribute {
    name = "commandId"
    type = "S"
  }

  ttl {
    attribute_name = "ttl"
    enabled        = true
  }

  tags = {
    Project = var.project_name
  }
}
