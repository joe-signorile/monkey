"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, GetCommand, PutCommand } = require("@aws-sdk/lib-dynamodb");
const { IoTDataPlaneClient, PublishCommand } = require("@aws-sdk/client-iot-data-plane");
const crypto = require("crypto");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const iotData = new IoTDataPlaneClient({ endpoint: `https://${process.env.IOT_ENDPOINT}` });
const DEVICES_TABLE = process.env.FLEET_DEVICES_TABLE;
const COMMANDS_TABLE = process.env.FLEET_COMMANDS_TABLE;
const VALID_TYPES = new Set(["SET_VOLUME", "LOCK", "UNLOCK"]);

const ok = (body) => ({ statusCode: 200, body: JSON.stringify(body) });
const bad = (statusCode, message) => ({ statusCode, body: JSON.stringify({ error: message }) });

function newCommandId() {
  // Date.now() sorts naturally as the range key; the suffix only breaks ties within the
  // same millisecond — no need for a real ULID library at this write volume.
  return `${Date.now()}-${crypto.randomBytes(3).toString("hex")}`;
}

// fleet-commands is the audit/delivery trail, not the transport — IoT Core's publish is
// what actually reaches the device (per the plan's C1 transport decision). Written before
// publish so the audit row exists even if the publish fails; the row's status just stays
// PENDING in that case rather than getting acked (no ack-write path exists yet — devices
// don't currently report command completion, so PENDING is the only status this ever
// writes).
async function writeAndPublish(deviceId, type, payload) {
  const commandId = newCommandId();
  await ddb.send(
    new PutCommand({
      TableName: COMMANDS_TABLE,
      Item: { deviceId, commandId, type, payload: payload ?? null, status: "PENDING", createdAt: Date.now() },
    })
  );
  await iotData.send(
    new PublishCommand({
      topic: `fleet/${deviceId}/commands`,
      payload: JSON.stringify({ commandId, type, payload: payload ?? null }),
    })
  );
  return commandId;
}

exports.handler = async (event) => {
  let body;
  try {
    body = JSON.parse(event.body || "{}");
  } catch {
    return bad(400, "invalid JSON body");
  }
  const { type, payload } = body;
  if (!VALID_TYPES.has(type)) return bad(400, `type must be one of ${[...VALID_TYPES].join(", ")}`);

  const deviceId = event.pathParameters && event.pathParameters.id;
  if (!deviceId) return bad(400, "device id is required");

  const { Item: device } = await ddb.send(new GetCommand({ TableName: DEVICES_TABLE, Key: { deviceId } }));
  if (!device) return bad(404, "device not found");

  const commandId = await writeAndPublish(deviceId, type, payload);
  return ok({ commandId });
};
