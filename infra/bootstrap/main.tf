# infra/bootstrap — remote state backend, applied once, by hand, before infra/ exists.
#
# This module has no backend of its own (local state only) — that's not an oversight,
# it's the chicken-and-egg the bootstrapping pattern exists to solve: infra/ can't
# depend on an S3 bucket/DynamoDB table that Terraform hasn't created yet.
#
# Usage:
#   cd infra/bootstrap
#   terraform init
#   terraform apply
#   # then hand the bucket/table names to infra/backend.tf (already wired to the
#   # default names below — only edit backend.tf if you changed project_name here).
#
# Keep this directory's terraform.tfstate safe (back it up outside git) — losing it
# doesn't lose the bucket/table themselves, but it does lose Terraform's record of
# owning them, meaning a future change here would need a manual `terraform import`.

terraform {
  required_version = ">= 1.5"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = var.aws_region
}

# Versioned so a bad apply's state is recoverable; encrypted and public-access-blocked
# because Terraform state routinely contains ARNs, resource IDs, and sometimes secrets
# in plan output — treat it as sensitive by default even though nothing in this specific
# project's state is expected to be.
resource "aws_s3_bucket" "state" {
  bucket = "${var.project_name}-terraform-state"

  tags = {
    Project = var.project_name
  }
}

resource "aws_s3_bucket_versioning" "state" {
  bucket = aws_s3_bucket.state.id

  versioning_configuration {
    status = "Enabled"
  }
}

resource "aws_s3_bucket_server_side_encryption_configuration" "state" {
  bucket = aws_s3_bucket.state.id

  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

resource "aws_s3_bucket_public_access_block" "state" {
  bucket = aws_s3_bucket.state.id

  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

# PK must be named exactly "LockID" — that's the attribute name the S3 backend's
# native locking looks for, not a naming choice made here.
resource "aws_dynamodb_table" "locks" {
  name         = "${var.project_name}-terraform-locks"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "LockID"

  attribute {
    name = "LockID"
    type = "S"
  }

  tags = {
    Project = var.project_name
  }
}
