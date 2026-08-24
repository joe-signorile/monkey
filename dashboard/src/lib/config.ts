// Build-time env injection (Vite/SvelteKit static env) — matches the static-adapter,
// no-server-runtime deploy target: there's no server around at request time to source
// dynamic env from, so these get baked into the bundle at build. Names match
// infra/outputs.tf 1:1; see .env.example.
import {
	PUBLIC_API_ENDPOINT,
	PUBLIC_COGNITO_HOSTED_UI_DOMAIN,
	PUBLIC_COGNITO_CLIENT_ID
} from '$env/static/public';

export const config = {
	apiEndpoint: PUBLIC_API_ENDPOINT.replace(/\/+$/, ''),
	cognitoHostedUiDomain: PUBLIC_COGNITO_HOSTED_UI_DOMAIN,
	cognitoClientId: PUBLIC_COGNITO_CLIENT_ID,
	// Must exactly match a callback URL registered on the Cognito app client
	// (infra/main.tf: "http://localhost:5173/callback" for dev, the CloudFront domain's
	// "/callback" in prod) — computed from the live origin so one build works in both.
	redirectUri: () => `${window.location.origin}/callback`
};
