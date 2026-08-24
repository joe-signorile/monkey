// Root has no content of its own — land authenticated visitors on the device list. The
// root +layout guard has already redirected unauthenticated visitors to /login by the
// time this would run.
import { redirect } from '@sveltejs/kit';

export function load() {
	redirect(302, '/devices');
}
