<script lang="ts">
	import { config } from '$lib/config';
	import { generateVerifier, challengeFromVerifier } from '$lib/auth/pkce';

	// Transient, not part of the session store — needed only across the redirect to the
	// Hosted UI and back to /callback, sessionStorage is the right lifetime for it too.
	const VERIFIER_KEY = 'monkey-fleet-pkce-verifier';

	async function login() {
		const verifier = generateVerifier();
		sessionStorage.setItem(VERIFIER_KEY, verifier);
		const challenge = await challengeFromVerifier(verifier);

		const authorizeUrl = new URL(`https://${config.cognitoHostedUiDomain}/oauth2/authorize`);
		authorizeUrl.searchParams.set('response_type', 'code');
		authorizeUrl.searchParams.set('client_id', config.cognitoClientId);
		authorizeUrl.searchParams.set('redirect_uri', config.redirectUri());
		authorizeUrl.searchParams.set('scope', 'openid email');
		authorizeUrl.searchParams.set('code_challenge', challenge);
		authorizeUrl.searchParams.set('code_challenge_method', 'S256');

		window.location.href = authorizeUrl.toString();
	}
</script>

<h1>monkey-fleet</h1>
<p class="muted">Sign in to manage paired devices.</p>
<button onclick={login}>Log in with Cognito</button>
