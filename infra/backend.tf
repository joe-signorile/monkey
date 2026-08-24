# Backend blocks can't reference variables or other resources — Terraform has to know
# where state lives before it evaluates anything else. These values are the fixed names
# infra/bootstrap/main.tf derives from its own project_name/aws_region defaults
# ("monkey-fleet" / "us-east-1"). If you changed either default when running bootstrap,
# update the matching values here by hand.
terraform {
  backend "s3" {
    bucket         = "monkey-fleet-terraform-state"
    key            = "infra/terraform.tfstate"
    region         = "us-east-1"
    dynamodb_table = "monkey-fleet-terraform-locks"
    encrypt        = true
  }
}
