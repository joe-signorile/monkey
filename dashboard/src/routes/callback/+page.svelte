<script lang="ts">
	import { onMount } from 'svelte';
	import { goto } from '$app/navigation';
	import { config } from '$lib/config';
	import { session } from '$lib/session';

	const VERIFIER_KEY = 'monkey-fleet-pkce-verifier';

	let error = $state<string | null>(null);

	interface TokenResponse {
		id_token: string;
		access_token: string;
		refresh_token?: string;
		expires_in: number;
	}

	onMount(async () => {
		const params = new URLSearchParams(window.location.search);
		const oauthError = params.get('error');
		if (oauthError) {
			error = params.get('error_description') ?? oauthError;
			return;
		}

		const code = params.get('code');
		const verifier = sessionStorage.getItem(VERIFIER_KEY);
		if (!code || !verifier) {
			error = 'missing authorization code or PKCE verifier — try logging in again';
			return;
		}

		try {
			const res = await fetch(`https://${config.cognitoHostedUiDomain}/oauth2/token`, {
				method: 'POST',
				headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
				body: new URLSearchParams({
					grant_type: 'authorization_code',
					client_id: config.cognitoClientId,
					code,
					redirect_uri: config.redirectUri(),
					code_verifier: verifier
				})
			});
			sessionStorage.removeItem(VERIFIER_KEY);

			if (!res.ok) {
				const body = await res.json().catch(() => ({}));
				error = body.error_description ?? body.error ?? `token exchange failed: ${res.status}`;
				return;
			}

			const tokens: TokenResponse = await res.json();
			session.set({
				idToken: tokens.id_token,
				accessToken: tokens.access_token,
				refreshToken: tokens.refresh_token,
				expiresAt: Date.now() + tokens.expires_in * 1000
			});
			goto('/devices');
		} catch (e) {
			error = e instanceof Error ? e.message : 'token exchange failed';
		}
	});
</script>

<h1>Signing in&hellip;</h1>
{#if error}
	<p class="error">{error}</p>
	<a href="/login">Try again</a>
{/if}
