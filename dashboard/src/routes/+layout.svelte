<script lang="ts">
	import '../app.css';
	import favicon from '$lib/assets/favicon.svg';
	import { page } from '$app/state';
	import { session, clearSession, isValid } from '$lib/session';

	let { children } = $props();

	function logout() {
		clearSession();
		window.location.href = '/login';
	}
</script>

<svelte:head>
	<link rel="icon" href={favicon} />
</svelte:head>

{#if page.url.pathname !== '/login' && page.url.pathname !== '/callback'}
	<nav class="topbar">
		<a href="/devices">monkey-fleet</a>
		<div class="links">
			<a href="/devices">Devices</a>
			<a href="/pair">Pair device</a>
			{#if isValid($session)}
				<button class="secondary" onclick={logout}>Log out</button>
			{/if}
		</div>
	</nav>
{/if}

<div class="page">
	{@render children()}
</div>
