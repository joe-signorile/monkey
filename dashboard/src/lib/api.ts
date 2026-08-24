// Thin fetch wrapper: base URL + auth header injection + typed shapes matching the Lambda
// handlers in infra/modules/api/lambda/*/index.js exactly (field names/casing read from
// the handler bodies, not guessed).
import { goto } from '$app/navigation';
import { config } from './config';
import { currentIdToken, clearSession } from './session';

// fleet-devices item shape, per infra/modules/dynamo/main.tf's schema comment and what
// pairing-claim/index.js actually writes. batteryLevel/installedApps aren't written by
// any Lambda in this repo — they land via telemetry the device reports directly (C1/C2,
// device-side work not yet built), so they're absent until a device has checked in at
// least once. installedApps' element shape is a guess: no handler defines it, so this
// mirrors the one place a package-name array shape *is* defined server-side (allowlist's
// `pkgs: string[]`) rather than inventing a richer shape with no contract behind it.
export interface Device {
	deviceId: string;
	name: string;
	pairedAt: number;
	lastCheckIn: number | null;
	allowList: string[];
	lockState: 'unknown' | 'locked' | 'unlocked';
	desiredLockState: 'locked' | 'unlocked';
	batteryLevel?: number;
	installedApps?: string[];
}

export type CommandType = 'SET_VOLUME' | 'LOCK' | 'UNLOCK';
export type CommandPayload = { level: number } | undefined;

export class ApiError extends Error {
	constructor(
		public status: number,
		message: string
	) {
		super(message);
	}
}

async function request<T>(path: string, opts: RequestInit = {}): Promise<T> {
	const token = currentIdToken();
	const headers: Record<string, string> = {
		'Content-Type': 'application/json',
		...(opts.headers as Record<string, string>)
	};
	if (token) headers['Authorization'] = `Bearer ${token}`;

	const res = await fetch(`${config.apiEndpoint}${path}`, { ...opts, headers });

	if (res.status === 401 || res.status === 403) {
		// Token missing/expired/rejected server-side: don't leave the page silently
		// broken on a failed fetch, send the operator back through the login flow.
		clearSession();
		goto('/login');
		throw new ApiError(res.status, 'not authenticated');
	}

	const body = await res.json().catch(() => ({}));
	if (!res.ok) throw new ApiError(res.status, body.error ?? `request failed: ${res.status}`);
	return body as T;
}

export const api = {
	devicesList: () => request<Device[]>('/devices'),

	devicesAllowlist: (deviceId: string, pkgs: string[]) =>
		request<{ allowList: string[] }>(`/devices/${encodeURIComponent(deviceId)}/allowlist`, {
			method: 'PUT',
			body: JSON.stringify({ pkgs })
		}),

	deviceCommand: (deviceId: string, type: CommandType, payload?: CommandPayload) =>
		request<{ commandId: string }>(`/devices/${encodeURIComponent(deviceId)}/commands`, {
			method: 'POST',
			body: JSON.stringify({ type, payload })
		}),

	commandsFanout: (type: CommandType, payload?: CommandPayload) =>
		request<{ commands: { deviceId: string; commandId: string }[] }>('/commands', {
			method: 'POST',
			body: JSON.stringify({ type, payload })
		}),

	// Cognito-authed (operator names the device in the UI) — distinct from the
	// unauthenticated device-facing pairing-request/pairing-status polls, which the
	// dashboard never calls.
	pairingClaim: (pairingCode: string, deviceName: string) =>
		request<Device>('/pairing/claim', {
			method: 'POST',
			body: JSON.stringify({ pairingCode, deviceName })
		})
};
