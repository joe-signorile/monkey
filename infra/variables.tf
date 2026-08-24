variable "aws_region" {
  description = "AWS region for every resource in this config."
  type        = string
  default     = "us-east-1"
}

# Prefixes/tags every resource across every module — single knob for cost tracking (a
# Cost Explorer filter on the Project tag) and for keeping names collision-free within
# the account, since this is one account with no dev/prod split to separate on.
variable "project_name" {
  description = "Prefix for resource names and the Project tag applied everywhere."
  type        = string
  default     = "monkey-fleet"
}
