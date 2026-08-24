import { writable, get } from 'svelte/store';
import { browser } from '$app/environment';

export interface Session {
	// Cognito's access token has no `aud` claim (it carries `client_id` instead); the
	// API Gateway JWT authorizer is configured with `audience = [client_id]`, which only
	// the id token's `aud` claim satisfies. So the id token, not the access token, is
	// what rides as the API bearer — counterintuitive but required by how the authorizer
	// is wired in infra/modules/api/main.tf.
	idToken: string;
	accessToken: string;
	refreshToken?: string;
	expiresAt: number; // epoch ms
}

const STORAGE_KEY = 'monkey-fleet-session';

// sessionStorage, not localStorage: this is a shared/family-adjacent browser and a
// dashboard login shouldn't persist indefinitely once the tab/window closes.
function load(): Session | null {
	if (!browser) return null;
	const raw = sessionStorage.getItem(STORAGE_KEY);
	if (!raw) return null;
	try {
		return JSON.parse(raw) as Session;
	} catch {
		return null;
	}
}

export const session = writable<Session | null>(load());

session.subscribe((value) => {
	if (!browser) return;
	if (value) sessionStorage.setItem(STORAGE_KEY, JSON.stringify(value));
	else sessionStorage.removeItem(STORAGE_KEY);
});

export function isValid(s: Session | null): s is Session {
	return !!s && s.expiresAt > Date.now();
}

export function currentIdToken(): string | null {
	const s = get(session);
	return isValid(s) ? s.idToken : null;
}

export function clearSession(): void {
	session.set(null);
}
