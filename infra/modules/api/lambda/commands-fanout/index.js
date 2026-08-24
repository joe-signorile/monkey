"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, ScanCommand, PutCommand } = require("@aws-sdk/lib-dynamodb");
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
  return `${Date.now()}-${crypto.randomBytes(3).toString("hex")}`;
}

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

// Fan-out at write time: this Lambda iterates every device and publishes one command
// each, so the device side never has to know whether a command targeted it specifically
// or the whole fleet (per the plan's C3 design) — device-commands and this handler share
// the write/publish shape but aren't factored into a shared module (see infra/modules/api
// notes: 7 small handlers, no Lambda Layer, a little duplication is the simpler call here).
exports.handler = async (event) => {
  let body;
  try {
    body = JSON.parse(event.body || "{}");
  } catch {
    return bad(400, "invalid JSON body");
  }
  const { type, payload } = body;
  if (!VALID_TYPES.has(type)) return bad(400, `type must be one of ${[...VALID_TYPES].join(", ")}`);

  const { Items: devices } = await ddb.send(
    new ScanCommand({ TableName: DEVICES_TABLE, ProjectionExpression: "deviceId" })
  );

  const commands = await Promise.all(
    (devices || []).map((d) =>
      writeAndPublish(d.deviceId, type, payload).then((commandId) => ({ deviceId: d.deviceId, commandId }))
    )
  );

  return ok({ commands });
};
