import { afterAll, beforeAll, it } from "vitest";
import { LiveServer } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";
let server: LiveServer;
beforeAll(async () => { const t = await liveTarget(); server = await LiveServer.start({ projects: [t.uproject] }); }, 120_000);
afterAll(async () => { await server?.stop?.(); await closeLiveBridges(); });
const show = (label: string, r: unknown) => console.log("PROOF", label, JSON.stringify(r).slice(0, 900));
it("proofs", async () => {
  const calls: Array<[string, Record<string, unknown>]> = JSON.parse(process.env.PROOF_CALLS ?? "[]");
  for (const [tool, args] of calls) show(`${tool}.${args.action}`, await server.call(tool, args));
}, 600_000);
