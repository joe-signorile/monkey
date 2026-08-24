output "api_endpoint" {
  value = module.api.api_endpoint
}

output "cloudfront_domain" {
  value = module.frontend.cloudfront_domain
}

output "cognito_user_pool_id" {
  value = module.auth.user_pool_id
}

output "cognito_client_id" {
  value = module.auth.client_id
}

output "cognito_hosted_ui_domain" {
  value = module.auth.hosted_ui_domain
}

output "iot_endpoint" {
  value = module.iot.endpoint
}
