variable "aws_region" {
  description = "AWS region for the state bucket/lock table."
  type        = string
  default     = "us-east-1"
}

variable "project_name" {
  description = "Prefix for resource names; must match infra/variables.tf's project_name, since infra/backend.tf hardcodes names derived from it."
  type        = string
  default     = "monkey-fleet"
}
