# admin_create_user_config blocks self-signup entirely — this pool has exactly one
# intended user (the operator running the dashboard), created once via the AWS console/CLI
# after apply, not through a signup flow that doesn't need to exist.
resource "aws_cognito_user_pool" "this" {
  name = "${var.project_name}-users"

  admin_create_user_config {
    allow_admin_create_user_only = true
  }

  tags = {
    Project = var.project_name
  }
}

# No client secret: this client is used from a browser SPA with PKCE, which is designed
# specifically so a public client never needs to hold a secret it can't keep confidential.
resource "aws_cognito_user_pool_client" "dashboard" {
  name         = "${var.project_name}-dashboard"
  user_pool_id = aws_cognito_user_pool.this.id

  generate_secret = false

  allowed_oauth_flows                  = ["code"]
  allowed_oauth_flows_user_pool_client = true
  allowed_oauth_scopes                 = ["openid", "email"]
  supported_identity_providers         = ["COGNITO"]

  callback_urls = var.callback_urls
  logout_urls   = var.logout_urls

  explicit_auth_flows = ["ALLOW_REFRESH_TOKEN_AUTH", "ALLOW_USER_SRP_AUTH"]
}

# Hosted UI domain prefix — must be globally unique across all Cognito accounts, not just
# this one. project_name's default ("monkey-fleet") is unlikely to collide but isn't
# guaranteed to be free; if apply fails on this resource, override project_name.
resource "aws_cognito_user_pool_domain" "this" {
  domain       = "${var.project_name}-auth"
  user_pool_id = aws_cognito_user_pool.this.id
}
