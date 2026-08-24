output "table_names" {
  value = {
    fleet-devices       = aws_dynamodb_table.fleet_devices.name
    fleet-pairing-codes = aws_dynamodb_table.fleet_pairing_codes.name
    fleet-commands      = aws_dynamodb_table.fleet_commands.name
  }
}

output "table_arns" {
  value = {
    fleet-devices       = aws_dynamodb_table.fleet_devices.arn
    fleet-pairing-codes = aws_dynamodb_table.fleet_pairing_codes.arn
    fleet-commands      = aws_dynamodb_table.fleet_commands.arn
  }
}
