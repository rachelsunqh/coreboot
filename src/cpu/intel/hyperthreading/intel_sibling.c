/*
 * This file is part of the coreboot project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/x86/lapic.h>
#include <cpu/intel/hyperthreading.h>
#include <device/device.h>
#include <pc80/mc146818rtc.h>
#include <smp/spinlock.h>
#include <assert.h>

#if IS_ENABLED(CONFIG_PARALLEL_CPU_INIT)
#error Intel hyper-threading requires serialized CPU init
#endif

static int first_time = 1;
static int disable_siblings = !CONFIG_LOGICAL_CPUS;

/* Return true if running thread does not have the smallest lapic ID
 * within a CPU core.
 */
int intel_ht_sibling(void)
{
	unsigned int core_ids, apic_ids, threads;

	apic_ids = 1;
	if (cpuid_eax(0) >= 1)
		apic_ids = (cpuid_ebx(1) >> 16) & 0xff;
	if (apic_ids < 1)
		apic_ids = 1;

	core_ids = 1;
	if (cpuid_eax(0) >= 4) {
		struct cpuid_result result;
		result = cpuid_ext(4, 0);
		core_ids += (result.eax >> 26) & 0x3f;
	}

	threads = (apic_ids / core_ids);
	return !!(lapicid() & (threads-1));
}

void intel_sibling_init(struct device *cpu)
{
	unsigned int i, siblings;
	struct cpuid_result result;

	/* On the bootstrap processor see if I want sibling cpus enabled */
	if (first_time) {
		first_time = 0;
		get_option(&disable_siblings, "hyper_threading");
	}
	result = cpuid(1);
	/* Is hyperthreading supported */
	if (!(result.edx & (1 << 28)))
		return;

	/* See how many sibling cpus we have */
	siblings = (result.ebx >> 16) & 0xff;
	if (siblings < 1)
		siblings = 1;

	printk(BIOS_DEBUG, "CPU: %u %d siblings\n",
		cpu->path.apic.apic_id,
		siblings);

	/* See if I am a sibling cpu */
	if (cpu->path.apic.apic_id & (siblings - 1)) {
		if (disable_siblings)
			cpu->enabled = 0;
		return;
	}

	/* I am the primary CPU start up my siblings */
	for (i = 1; i < siblings; i++) {
		struct device_path cpu_path;
		struct device *new;
		/* Build the CPU device path */
		cpu_path.type = DEVICE_PATH_APIC;
		cpu_path.apic.apic_id = cpu->path.apic.apic_id + i;


		/* Allocate new CPU device structure iff sibling CPU
		 * was not in static device tree.
		 */
		new = alloc_find_dev(cpu->bus, &cpu_path);

		if (!new)
			continue;

		printk(BIOS_DEBUG, "CPU: %u has sibling %u\n",
			cpu->path.apic.apic_id,
			new->path.apic.apic_id);
	}
}
