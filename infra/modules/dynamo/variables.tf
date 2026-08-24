variable "project_name" {
  description = "Used for tagging; also prefixes nothing here since table names are fixed by the API contract the Lambdas expect."
  type        = string
}
