import { defineConfig } from "vitest/config";

/**
 * The integration suite (tests/integration): every case spawns the real CLI,
 * server or daemon in its own sandbox, so files run in parallel forks. The
 * unit suite stays single-fork; it shares process state these never touch.
 */
export default defineConfig({
  test: {
    include: ["tests/integration/**/*.test.ts"],
    testTimeout: 120_000,
    hookTimeout: 120_000,
    reporters: ["verbose"],
    fileParallelism: true,
    pool: "forks",
    poolOptions: { forks: { singleFork: false } },
  },
});
