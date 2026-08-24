<script lang="ts">
	import { onMount } from 'svelte';
	import { page } from '$app/state';
	import { api, ApiError, type Device } from '$lib/api';

	// The [id] segment always matches for this route; the assertion just satisfies
	// $app/state's generic (route-agnostic) params typing.
	const deviceId = page.params.id as string;

	let device = $state<Device | null>(null);
	let loading = $state(true);
	let loadError = $state<string | null>(null);

	let selectedApps = $state<Set<string>>(new Set());
	let allowListStatus = $state<string | null>(null);

	let volume = $state(50);
	let volumeStatus = $state<string | null>(null);
	let lockStatus = $state<string | null>(null);

	// No GET /devices/{id} route exists (infra/modules/api/main.tf only defines
	// GET /devices, list-only) — fetch the full list and pick this device out of it. A
	// per-device GET would be the better fit for this page; flagging rather than
	// guessing a route the backend doesn't have.
	async function load() {
		loading = true;
		loadError = null;
		try {
			const devices = await api.devicesList();
			const found = devices.find((d) => d.deviceId === deviceId);
			if (!found) {
				loadError = 'device not found';
				return;
			}
			device = found;
			selectedApps = new Set(found.allowList);
		} catch (e) {
			loadError = e instanceof ApiError ? e.message : 'failed to load device';
		} finally {
			loading = false;
		}
	}

	onMount(load);

	function toggleApp(pkg: string) {
		const next = new Set(selectedApps);
		if (next.has(pkg)) next.delete(pkg);
		else next.add(pkg);
		selectedApps = next;
	}

	async function saveAllowList() {
		allowListStatus = 'saving…';
		try {
			const { allowList } = await api.devicesAllowlist(deviceId, [...selectedApps]);
			if (device) device = { ...device, allowList };
			allowListStatus = 'saved';
		} catch (e) {
			allowListStatus = e instanceof ApiError ? e.message : 'failed to save';
		}
	}

	async function setVolume() {
		volumeStatus = 'sending…';
		try {
			await api.deviceCommand(deviceId, 'SET_VOLUME', { level: volume });
			volumeStatus = 'command sent';
		} catch (e) {
			volumeStatus = e instanceof ApiError ? e.message : 'failed to send';
		}
	}

	async function sendLock(type: 'LOCK' | 'UNLOCK') {
		lockStatus = 'sending…';
		try {
			await api.deviceCommand(deviceId, type);
			lockStatus = `${type.toLowerCase()} command sent`;
		} catch (e) {
			lockStatus = e instanceof ApiError ? e.message : 'failed to send';
		}
	}
</script>

{#if loading}
	<p class="muted">Loading…</p>
{:else if loadError}
	<p class="error">{loadError}</p>
{:else if device}
	<h1>{device.name}</h1>
	<p class="muted">{device.deviceId}</p>

	<div class="card">
		<h2>Lock state</h2>
		<!-- Last-reported vs desired: a sent command hasn't necessarily been applied yet. -->
		<p>
			Last reported: <span class="badge {device.lockState}">{device.lockState}</span>
			&nbsp;·&nbsp;
			Desired: <span class="badge {device.desiredLockState}">{device.desiredLockState}</span>
		</p>
		<form class="inline">
			<button type="button" onclick={() => sendLock('LOCK')}>Lock</button>
			<button type="button" class="secondary" onclick={() => sendLock('UNLOCK')}>Unlock</button>
			{#if lockStatus}<span class="muted">{lockStatus}</span>{/if}
		</form>
	</div>

	<div class="card">
		<h2>Volume</h2>
		<form class="inline" onsubmit={(e) => (e.preventDefault(), setVolume())}>
			<input type="range" min="0" max="100" bind:value={volume} />
			<span>{volume}</span>
			<button type="submit">Set volume</button>
			{#if volumeStatus}<span class="muted">{volumeStatus}</span>{/if}
		</form>
	</div>

	<div class="card">
		<h2>Installed apps</h2>
		{#if !device.installedApps || device.installedApps.length === 0}
			<p class="muted">No installed-apps report from this device yet.</p>
		{:else}
			<table>
				<thead>
					<tr>
						<th>Package</th>
						<th>Allowed</th>
					</tr>
				</thead>
				<tbody>
					{#each device.installedApps as pkg (pkg)}
						<tr>
							<td>{pkg}</td>
							<td>{device.allowList.length === 0 ? 'all' : device.allowList.includes(pkg) ? 'yes' : 'no'}</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{/if}
	</div>

	<div class="card">
		<h2>Allow-list editor</h2>
		{#if !device.installedApps || device.installedApps.length === 0}
			<p class="muted">Nothing to edit until this device reports installed apps.</p>
		{:else}
			<ul style="list-style:none; padding:0;">
				{#each device.installedApps as pkg (pkg)}
					<li>
						<label style="display:flex; align-items:center; gap:0.5rem;">
							<input
								type="checkbox"
								checked={selectedApps.has(pkg)}
								onchange={() => toggleApp(pkg)}
							/>
							{pkg}
						</label>
					</li>
				{/each}
			</ul>
			<form class="inline" onsubmit={(e) => (e.preventDefault(), saveAllowList())}>
				<button type="submit">Save allow-list</button>
				{#if allowListStatus}<span class="muted">{allowListStatus}</span>{/if}
			</form>
		{/if}
	</div>
{/if}
