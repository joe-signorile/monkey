"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, PutCommand } = require("@aws-sdk/lib-dynamodb");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const TABLE = process.env.FLEET_PAIRING_CODES_TABLE;
const CODE_TTL_SECONDS = 600;
// Excludes 0/O/1/I — a kid or operator squinting at a 6-char code on a tablet screen is
// the threat model here, not brute-force; this is not a security control.
const CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

const ok = (body) => ({ statusCode: 200, body: JSON.stringify(body) });
const bad = (statusCode, message) => ({ statusCode, body: JSON.stringify({ error: message }) });

function randomCode() {
  let code = "";
  for (let i = 0; i < 6; i++) code += CHARS[Math.floor(Math.random() * CHARS.length)];
  return code;
}

exports.handler = async (event) => {
  let body;
  try {
    body = JSON.parse(event.body || "{}");
  } catch {
    return bad(400, "invalid JSON body");
  }
  const { deviceId } = body;
  if (!deviceId || typeof deviceId !== "string") return bad(400, "deviceId is required");

  const now = Math.floor(Date.now() / 1000);
  const expiresAt = now + CODE_TTL_SECONDS;

  // Conditional put + retry-on-collision instead of a pre-check GetItem: cheap at this
  // scale (6-char alphanumeric space), and avoids a check-then-act race between two
  // devices pairing at once.
  for (let attempt = 0; attempt < 5; attempt++) {
    const pairingCode = randomCode();
    try {
      await ddb.send(
        new PutCommand({
          TableName: TABLE,
          Item: { pairingCode, deviceId, claimed: false, expiresAt },
          ConditionExpression: "attribute_not_exists(pairingCode)",
        })
      );
      return ok({ pairingCode, expiresAt });
    } catch (e) {
      if (e.name !== "ConditionalCheckFailedException") throw e;
      // collision — loop and try a fresh random code
    }
  }
  return bad(500, "could not allocate a pairing code, try again");
};
