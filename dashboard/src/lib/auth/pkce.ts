// Hand-rolled PKCE (RFC 7636), not the Amplify SDK — the plan's explicit choice for an
// auth-only need this small. Only the two functions the code flow actually needs.

function base64url(bytes: Uint8Array): string {
	let str = '';
	for (const b of bytes) str += String.fromCharCode(b);
	return btoa(str).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export function generateVerifier(): string {
	return base64url(crypto.getRandomValues(new Uint8Array(32)));
}

export async function challengeFromVerifier(verifier: string): Promise<string> {
	const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(verifier));
	return base64url(new Uint8Array(digest));
}
