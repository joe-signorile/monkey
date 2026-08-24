"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, UpdateCommand } = require("@aws-sdk/lib-dynamodb");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const TABLE = process.env.FLEET_DEVICES_TABLE;

const ok = (body) => ({ statusCode: 200, body: JSON.stringify(body) });
const bad = (statusCode, message) => ({ statusCode, body: JSON.stringify({ error: message }) });

exports.handler = async (event) => {
  let body;
  try {
    body = JSON.parse(event.body || "{}");
  } catch {
    return bad(400, "invalid JSON body");
  }
  const { pkgs } = body;
  if (!Array.isArray(pkgs) || !pkgs.every((p) => typeof p === "string")) {
    return bad(400, "pkgs must be an array of strings");
  }

  const deviceId = event.pathParameters && event.pathParameters.id;
  if (!deviceId) return bad(400, "device id is required");

  // Current-state update, not a queued command: the device picks up allow-list changes
  // via its own report/poll path, not through fleet-commands — per the plan, allow-list
  // pushes and remote LOCK/UNLOCK/SET_VOLUME commands are deliberately different paths.
  try {
    const { Attributes } = await ddb.send(
      new UpdateCommand({
        TableName: TABLE,
        Key: { deviceId },
        UpdateExpression: "SET allowList = :pkgs",
        ExpressionAttributeValues: { ":pkgs": pkgs },
        ConditionExpression: "attribute_exists(deviceId)",
        ReturnValues: "ALL_NEW",
      })
    );
    return ok({ allowList: Attributes.allowList });
  } catch (e) {
    if (e.name === "ConditionalCheckFailedException") return bad(404, "device not found");
    throw e;
  }
};
