<script lang="ts">
	import { onMount } from 'svelte';
	import { api, ApiError, type Device } from '$lib/api';

	let devices = $state<Device[]>([]);
	let loading = $state(true);
	let loadError = $state<string | null>(null);

	let fleetVolume = $state(20);
	let fleetActionStatus = $state<string | null>(null);

	async function load() {
		loading = true;
		loadError = null;
		try {
			devices = await api.devicesList();
		} catch (e) {
			loadError = e instanceof ApiError ? e.message : 'failed to load devices';
		} finally {
			loading = false;
		}
	}

	onMount(load);

	function fmtCheckIn(ts: number | null): string {
		return ts ? new Date(ts).toLocaleString() : 'never';
	}

	// The concrete example driving this whole feature: one button, fans out to every
	// paired device via POST /commands (no {id}) rather than looping devicesList client-side.
	async function dropVolumeOnFleet() {
		fleetActionStatus = 'sending…';
		try {
			const { commands } = await api.commandsFanout('SET_VOLUME', { level: fleetVolume });
			fleetActionStatus = `sent to ${commands.length} device(s)`;
		} catch (e) {
			fleetActionStatus = e instanceof ApiError ? e.message : 'failed to send';
		}
	}
</script>

<h1>Devices</h1>

<div class="card">
	<h2>Fleet-wide: set volume on every device</h2>
	<form class="inline" onsubmit={(e) => (e.preventDefault(), dropVolumeOnFleet())}>
		<input type="number" min="0" max="100" bind:value={fleetVolume} />
		<button type="submit">Set volume on all devices</button>
		{#if fleetActionStatus}<span class="muted">{fleetActionStatus}</span>{/if}
	</form>
</div>

{#if loading}
	<p class="muted">Loading…</p>
{:else if loadError}
	<p class="error">{loadError}</p>
{:else if devices.length === 0}
	<p class="muted">No paired devices yet. <a href="/pair">Pair one</a>.</p>
{:else}
	<table>
		<thead>
			<tr>
				<th>Name</th>
				<th>Last check-in</th>
				<th>Battery</th>
				<th>Lock state</th>
			</tr>
		</thead>
		<tbody>
			{#each devices as d (d.deviceId)}
				<tr>
					<td><a href={`/devices/${d.deviceId}`}>{d.name}</a></td>
					<td>{fmtCheckIn(d.lastCheckIn)}</td>
					<td>{d.batteryLevel != null ? `${d.batteryLevel}%` : '—'}</td>
					<td><span class="badge {d.lockState}">{d.lockState}</span></td>
				</tr>
			{/each}
		</tbody>
	</table>
{/if}
