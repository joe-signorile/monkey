<script lang="ts">
	import { goto } from '$app/navigation';
	import { api, ApiError } from '$lib/api';

	let pairingCode = $state('');
	let deviceName = $state('');
	let error = $state<string | null>(null);
	let submitting = $state(false);

	async function submit() {
		error = null;
		submitting = true;
		try {
			const device = await api.pairingClaim(pairingCode.trim().toUpperCase(), deviceName.trim());
			goto(`/devices/${device.deviceId}`);
		} catch (e) {
			// Expired/unknown code surfaces here inline, per pairing-claim's 404 on a
			// missing or expired row — no dead-end redirect on a bad code.
			error = e instanceof ApiError ? e.message : 'failed to claim pairing code';
		} finally {
			submitting = false;
		}
	}
</script>

<h1>Pair a device</h1>
<p class="muted">Enter the code shown on the tablet's screen.</p>

<form class="card" onsubmit={(e) => (e.preventDefault(), submit())}>
	<div class="field">
		<label for="pairingCode">Pairing code</label>
		<input id="pairingCode" type="text" bind:value={pairingCode} required maxlength="6" />
	</div>
	<div class="field">
		<label for="deviceName">Device name</label>
		<input id="deviceName" type="text" bind:value={deviceName} required />
	</div>
	<button type="submit" disabled={submitting}>{submitting ? 'Pairing…' : 'Pair device'}</button>
	{#if error}<p class="error">{error}</p>{/if}
</form>
