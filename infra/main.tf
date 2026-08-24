module "dynamo" {
  source = "./modules/dynamo"

  project_name = var.project_name
}

module "iot" {
  source = "./modules/iot"

  project_name = var.project_name
  aws_region   = var.aws_region
}

module "frontend" {
  source = "./modules/frontend"

  project_name = var.project_name
}

# callback_urls includes the CloudFront domain once it exists, alongside the localhost
# dev default — Terraform resolves the module.frontend dependency automatically, no
# depends_on needed, since this is a plain output reference.
module "auth" {
  source = "./modules/auth"

  project_name  = var.project_name
  aws_region    = var.aws_region
  callback_urls = ["http://localhost:5173/callback", "https://${module.frontend.cloudfront_domain}/callback"]
  logout_urls   = ["http://localhost:5173/", "https://${module.frontend.cloudfront_domain}/"]
}

module "api" {
  source = "./modules/api"

  project_name                = var.project_name
  aws_region                  = var.aws_region
  dynamo_table_arns           = module.dynamo.table_arns
  dynamo_table_names          = module.dynamo.table_names
  iot_endpoint                = module.iot.endpoint
  iot_policy_document         = module.iot.policy_document
  cognito_user_pool_id        = module.auth.user_pool_id
  cognito_user_pool_client_id = module.auth.client_id
}
