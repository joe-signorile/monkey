"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, ScanCommand } = require("@aws-sdk/lib-dynamodb");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const TABLE = process.env.FLEET_DEVICES_TABLE;

exports.handler = async () => {
  // claudia: full table scan, no pagination — fine for a personal fleet's device
  // count; upgrade to a GSI + paginated query if this ever needs to list more devices
  // than fit in one Scan response.
  const { Items } = await ddb.send(new ScanCommand({ TableName: TABLE }));
  return { statusCode: 200, body: JSON.stringify(Items || []) };
};
