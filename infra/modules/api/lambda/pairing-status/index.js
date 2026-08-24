"use strict";
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, GetCommand, DeleteCommand } = require("@aws-sdk/lib-dynamodb");
const {
  IoTClient,
  CreateThingCommand,
  CreateKeysAndCertificateCommand,
  CreatePolicyCommand,
  AttachPolicyCommand,
  AttachThingPrincipalCommand,
} = require("@aws-sdk/client-iot");

const ddb = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const iot = new IoTClient({});
const TABLE = process.env.FLEET_PAIRING_CODES_TABLE;
const IOT_ENDPOINT = process.env.IOT_ENDPOINT;
const POLICY_DOCUMENT = process.env.IOT_POLICY_DOCUMENT;

// One shared policy for every device rather than one-per-device: the policy document's
// ${iot:Connection.Thing.ThingName} variables are resolved by IoT Core per-connection, so
// a single named policy already scopes each device to only its own topics — see the
// reasoning in infra/modules/iot/main.tf. iot:CreatePolicy errors on a duplicate name, so
// this is create-if-missing rather than assuming a first-run-only Terraform resource.
const SHARED_POLICY_NAME = "monkey-fleet-device-policy";

const ok = (body) => ({ statusCode: 200, body: JSON.stringify(body) });
const bad = (statusCode, message) => ({ statusCode, body: JSON.stringify({ error: message }) });

async function ensureSharedPolicy() {
  try {
    await iot.send(new CreatePolicyCommand({ policyName: SHARED_POLICY_NAME, policyDocument: POLICY_DOCUMENT }));
  } catch (e) {
    if (e.name !== "ResourceAlreadyExistsException") throw e;
  }
}

async function provisionDevice(deviceId) {
  // thingName = deviceId, per the plan. CreateThing is idempotent on an existing name
  // (returns the existing thing), which matters if a prior attempt got this far and then
  // failed before the row-delete below could ever run twice for the same code anyway —
  // belt-and-suspenders, not load-bearing on its own.
  await iot.send(new CreateThingCommand({ thingName: deviceId }));
  const { certificateArn, certificatePem, keyPair } = await iot.send(
    new CreateKeysAndCertificateCommand({ setAsActive: true })
  );
  await ensureSharedPolicy();
  await iot.send(new AttachPolicyCommand({ policyName: SHARED_POLICY_NAME, target: certificateArn }));
  await iot.send(new AttachThingPrincipalCommand({ thingName: deviceId, principal: certificateArn }));
  return { certificatePem, privateKey: keyPair.PrivateKey };
}

exports.handler = async (event) => {
  const code = event.queryStringParameters && event.queryStringParameters.code;
  if (!code) return bad(400, "code query parameter is required");

  const { Item: row } = await ddb.send(new GetCommand({ TableName: TABLE, Key: { pairingCode: code } }));
  const now = Math.floor(Date.now() / 1000);
  if (!row || row.expiresAt <= now) return bad(404, "pairing code not found or expired");

  if (!row.claimed) return ok({ claimed: false });

  // Single-use guard: delete the row first, conditioned on it still existing, BEFORE
  // provisioning. Two concurrent pollers hitting this after claim: only one delete
  // succeeds, the loser gets 404 and never touches IoT Core, so a duplicate cert can't be
  // issued from the same code. Deleting before (not after) provisioning means a mid-
  // provision crash strands the device with the row already gone — acceptable, since the
  // fix is just re-pairing, and it's the only ordering that actually closes the race
  // (delete-after leaves a window where two winners could both provision).
  try {
    await ddb.send(
      new DeleteCommand({
        TableName: TABLE,
        Key: { pairingCode: code },
        ConditionExpression: "attribute_exists(pairingCode)",
      })
    );
  } catch (e) {
    if (e.name === "ConditionalCheckFailedException") return bad(404, "pairing code not found or expired");
    throw e;
  }

  const { certificatePem, privateKey } = await provisionDevice(row.deviceId);
  // Never log certificatePem/privateKey: this is the one time the private key exists
  // anywhere retrievable, and it never gets stored server-side.
  return ok({ claimed: true, certificatePem, privateKey, iotEndpoint: IOT_ENDPOINT });
};
