// Plays VS Code's part against debug_adapter.js: launch, a breakpoint, the stack, variables, a step, then the end.
// node vscode_check.js debug_adapter.js program.jamo jamo.exe breakpoint_line
const { spawn } = require("child_process");
const [adapterPath, program, jamo, line] = process.argv.slice(2);
const adapter = spawn("node", [adapterPath]);
let sequence = 1;
let incoming = Buffer.alloc(0);
const waiting = [];
const seen = [];
const outputText = [];

function send(command, args) {
    const message = { seq: sequence++, type: "request", command, arguments: args };
    const body = Buffer.from(JSON.stringify(message));
    adapter.stdin.write("Content-Length: " + body.length + "\r\n\r\n");
    adapter.stdin.write(body);
    return new Promise(done => waiting.push({ match: m => m.type === "response" && m.request_seq === message.seq, done }));
}

function until(event) {
    const earlier = seen.find(m => m.type === "event" && m.event === event && !m.used);
    if (earlier) { earlier.used = true; return Promise.resolve(earlier); }
    return new Promise(done => waiting.push({ match: m => m.type === "event" && m.event === event, done }));
}

adapter.stdout.on("data", chunk => {
    incoming = Buffer.concat([incoming, chunk]);
    while (true) {
        const head = incoming.indexOf("\r\n\r\n");
        if (head < 0) return;
        const length = Number(/Content-Length: (\d+)/.exec(incoming.subarray(0, head).toString())[1]);
        if (incoming.length < head + 4 + length) return;
        const message = JSON.parse(incoming.subarray(head + 4, head + 4 + length).toString());
        incoming = incoming.subarray(head + 4 + length);
        if (message.event === "output") outputText.push(message.body.output);
        const at = waiting.findIndex(w => w.match(message));
        if (at >= 0) { message.used = true; waiting.splice(at, 1)[0].done(message); }
        seen.push(message);
    }
});

(async () => {
    const timer = setTimeout(() => { console.log("TIMED OUT"); process.exit(1); }, 20000);
    await send("initialize", { adapterID: "jamo" });
    const launched = await send("launch", { program, jamoPath: jamo });
    console.log("launch:", launched.success, launched.message || "");
    await until("initialized");
    const points = await send("setBreakpoints", { source: { path: program }, breakpoints: [{ line: Number(line) }] });
    console.log("breakpoints:", JSON.stringify(points.body.breakpoints));
    await send("configurationDone", {});
    const stop = await until("stopped");
    console.log("stopped:", stop.body.reason);
    const stack = await send("stackTrace", { threadId: 1 });
    for (const frame of stack.body.stackFrames) console.log("  frame", frame.id, frame.name, frame.source.name, frame.line);
    const scopes = await send("scopes", { frameId: 0 });
    const vars = await send("variables", { variablesReference: scopes.body.scopes[0].variablesReference });
    for (const v of vars.body.variables) console.log("  var", v.name + ":", v.type, "=", v.value);
    const up = await send("variables", { variablesReference: 2 });
    console.log("  one call up:", up.body.variables.map(v => v.name).join(", "));
    const hover = await send("evaluate", { expression: "amount", frameId: 0, context: "hover" });
    console.log("  hover amount:", hover.body.result);
    await send("next", { threadId: 1 });
    const stepped = await until("stopped");
    const again = await send("stackTrace", { threadId: 1 });
    console.log("after next:", stepped.body.reason, "line", again.body.stackFrames[0].line);
    await send("continue", { threadId: 1 });
    const exited = await until("exited");
    await until("terminated");
    console.log("exited:", exited.body.exitCode);
    console.log("program printed:", JSON.stringify(outputText.filter(t => !t.startsWith("dbg.jamo")).join("")));
    clearTimeout(timer);
    await send("disconnect", {});
    process.exit(0);
})();
