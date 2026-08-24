import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	// Runes mode is Svelte 5's default; no library code in this app to carve an exception
	// for, so the plain boolean (not the template's per-file function) is enough.
	compilerOptions: { runes: true },
	kit: {
		// Static build for S3+CloudFront, no server runtime. fallback: 'index.html' plus
		// `export const ssr = false` in the root layout makes this a client-only SPA —
		// every route (including dynamic ones like /devices/[id]) is auth-gated and
		// fetch-driven, nothing here is prerenderable ahead of time.
		adapter: adapter({
			fallback: 'index.html'
		})
	}
};

export default config;
