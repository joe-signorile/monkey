variable "project_name" {
  type = string
}

variable "acm_certificate_arn" {
  description = "ACM cert (must be in us-east-1 for CloudFront) for a custom domain. Left null until a Route53 zone/domain exists — CloudFront's default *.cloudfront.net domain + cert is used instead, so apply succeeds standalone."
  type        = string
  default     = null
}

variable "domain_aliases" {
  description = "Custom domain names for the distribution; only meaningful alongside acm_certificate_arn."
  type        = list(string)
  default     = []
}
