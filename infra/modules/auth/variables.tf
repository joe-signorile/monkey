variable "project_name" {
  type = string
}

variable "aws_region" {
  description = "Needed to build the hosted-UI domain's full hostname (*.auth.<region>.amazoncognito.com)."
  type        = string
}

variable "callback_urls" {
  description = "OAuth redirect URIs for the dashboard SPA. Defaults to localhost for local dev; the root module adds the CloudFront domain once the frontend module exists."
  type        = list(string)
  default     = ["http://localhost:5173/callback"]
}

variable "logout_urls" {
  type    = list(string)
  default = ["http://localhost:5173/"]
}
