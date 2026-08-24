"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, GetCommand, PutCommand, UpdateCommand } = require("@aws-sdk/lib-dynamodb");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const PAIRING_TABLE = process.env.FLEET_PAIRING_CODES_TABLE;
const DEVICES_TABLE = process.env.FLEET_DEVICES_TABLE;

const ok = (body) => ({ statusCode: 200, body: JSON.stringify(body) });
const bad = (statusCode, message) => ({ statusCode, body: JSON.stringify({ error: message }) });

exports.handler = async (event) => {
  let body;
  try {
    body = JSON.parse(event.body || "{}");
  } catch {
    return bad(400, "invalid JSON body");
  }
  const { pairingCode, deviceName } = body;
  if (!pairingCode || typeof pairingCode !== "string") return bad(400, "pairingCode is required");
  if (!deviceName || typeof deviceName !== "string") return bad(400, "deviceName is required");

  const { Item: row } = await ddb.send(new GetCommand({ TableName: PAIRING_TABLE, Key: { pairingCode } }));
  const now = Math.floor(Date.now() / 1000);
  if (!row || row.expiresAt <= now) return bad(404, "pairing code not found or expired");

  // Sensible v1 defaults: no allow-list yet (AppCatalog treats empty as "show
  // everything"), lock state unknown until the device's first check-in, but desired state
  // defaults to locked — a freshly paired tablet should come up restricted, not open.
  const device = {
    deviceId: row.deviceId,
    name: deviceName,
    pairedAt: now,
    lastCheckIn: null,
    allowList: [],
    lockState: "unknown",
    desiredLockState: "locked",
  };
  await ddb.send(new PutCommand({ TableName: DEVICES_TABLE, Item: device }));

  // Cert generation is deliberately NOT here — it happens on the device's next
  // pairing-status poll, which also deletes this row. This just flips the flag that poll
  // is waiting on.
  await ddb.send(
    new UpdateCommand({
      TableName: PAIRING_TABLE,
      Key: { pairingCode },
      UpdateExpression: "SET claimed = :true",
      ExpressionAttributeValues: { ":true": true },
    })
  );

  return ok(device);
};
