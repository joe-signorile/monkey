// No server runtime (static-adapter deploy target) — every route renders client-side
// only. Also doubles as where the "no valid token" guard lives: SvelteKit's redirect()
// works from a client-run load just like a server one.
import { redirect } from '@sveltejs/kit';
import { get } from 'svelte/store';
import { session, isValid } from '$lib/session';
import type { LayoutLoad } from './$types';

export const ssr = false;

const PUBLIC_PATHS = new Set(['/login', '/callback']);

export const load: LayoutLoad = ({ url }) => {
	if (!PUBLIC_PATHS.has(url.pathname) && !isValid(get(session))) {
		redirect(302, '/login');
	}
};
