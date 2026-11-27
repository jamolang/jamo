// The Jamo debugger for VS Code. VS Code talks to this over stdin and stdout in its Debug Adapter Protocol; this
// builds the program with jamo build --debug, runs it, and talks to it over a named pipe, one line at a time:
// the program says hello, stopped REASON LINE FILE, frame NAME|FILE|LINE ... end and var NAME|TYPE|VALUE ... end,
// and this says continue, step, next, out, pause, break LINE FILE, clear, frames and vars FRAME.
"use strict";
const { spawn, execFile } = require("child_process");
const net = require("net");
const path = require("path");

let sequence = 1;
let incoming = Buffer.alloc(0);
let root = "";                 // The program's folder: files are named from here, with / between folders
let child = null;
let socket = null;
let fromProgram = "";
let connected = false;
let configured = false;
let started = false;
let stopOnEntry = false;
const breakpoints = new Map();  // A file's full path, and its breakpoint lines
const questions = [];           // Waiting for their answer's end: { lines, done }
let answer = null;

// --- Talking to VS Code ---
function send(message) {
    message.seq = sequence++;
    const body = Buffer.from(JSON.stringify(message), "utf8");
    process.stdout.write("Content-Length: " + body.length + "\r\n\r\n");
    process.stdout.write(body);
}

function respond(request, body, message) {
    send({ type: "response", request_seq: request.seq, command: request.command, success: !message, message, body: body || {} });
}

function tell(event, body) {
    send({ type: "event", event, body: body || {} });
}

function output(text, category) {
    tell("output", { category: category || "console", output: text });
}

process.stdin.on("data", chunk => {
    incoming = Buffer.concat([incoming, chunk]);
    while (true) {
        const head = incoming.indexOf("\r\n\r\n");
        if (head < 0) return;
        const size = /Content-Length: *(\d+)/i.exec(incoming.subarray(0, head).toString());
        if (!size) {
            incoming = incoming.subarray(head + 4);
            continue;
        }
        const length = Number(size[1]);
        if (incoming.length < head + 4 + length) return;
        const message = JSON.parse(incoming.subarray(head + 4, head + 4 + length).toString("utf8"));
        incoming = incoming.subarray(head + 4 + length);
        handle(message).catch(problem => respond(message, {}, String(problem && problem.message || problem)));
    }
});

// --- Talking to the program ---
function toProgram(line) {
    if (socket && !socket.destroyed) socket.write(line + "\n");
}

function ask(question) {
    return new Promise(done => {
        if (!socket || socket.destroyed) return done([]);
        questions.push({ lines: [], done });
        toProgram(question);
    });
}

function label(file) {
    return path.relative(root, file).split(path.sep).join("/");
}

function sendBreakpoints() {
    toProgram("clear");
    for (const [file, lines] of breakpoints) {
        for (const line of lines) toProgram("break " + line + " " + label(file));
    }
}

function maybeStart() {
    if (connected && configured && !started) {
        started = true;
        toProgram(stopOnEntry ? "step" : "continue");
    }
}

function heard(line) {
    if (line === "hello") {
        connected = true;
        sendBreakpoints();
        maybeStart();
    } else if (line.startsWith("stopped ")) {
        const reason = line.split(" ")[1];
        tell("stopped", { reason: reason === "breakpoint" ? "breakpoint" : reason === "pause" ? "pause" : started ? "step" : "entry", threadId: 1, allThreadsStopped: true });
    } else if (line.startsWith("error ")) {
        output(line.slice(6) + "\n", "stderr");
    } else if (questions.length > 0) {
        if (line === "end") {
            const question = questions.shift();
            question.done(question.lines);
        } else {
            questions[0].lines.push(line);
        }
    }
}

// NAME|TYPE|VALUE, where only the value can hold a |
function parts(line, count) {
    const out = [];
    let rest = line;
    for (let i = 0; i < count - 1; i++) {
        const at = rest.indexOf("|");
        out.push(at < 0 ? rest : rest.slice(0, at));
        rest = at < 0 ? "" : rest.slice(at + 1);
    }
    out.push(rest);
    return out;
}

async function variables(frame) {
    const lines = await ask("vars " + frame);
    return lines.filter(line => line.startsWith("var ")).map(line => {
        const [name, type, value] = parts(line.slice(4), 3);
        return { name, type, value, variablesReference: 0 };
    });
}

// --- VS Code's requests ---
async function handle(request) {
    const args = request.arguments || {};
    switch (request.command) {
        case "initialize":
            respond(request, { supportsConfigurationDoneRequest: true, supportsTerminateRequest: true, supportsEvaluateForHovers: true });
            return;
        case "launch":
            return launch(request, args);
        case "setBreakpoints": {
            const file = args.source && args.source.path;
            const lines = (args.breakpoints || []).map(point => point.line);
            if (file) breakpoints.set(file, lines);
            if (connected) sendBreakpoints();
            respond(request, { breakpoints: lines.map(line => ({ verified: true, line })) });
            return;
        }
        case "configurationDone":
            configured = true;
            respond(request);
            maybeStart();
            return;
        case "threads":
            respond(request, { threads: [{ id: 1, name: "Jamo program" }] });
            return;
        case "stackTrace": {
            const frames = (await ask("frames")).filter(line => line.startsWith("frame ")).map((line, id) => {
                const [name, file, number] = parts(line.slice(6), 3);
                const full = path.join(root, ...file.split("/"));
                return { id, name, source: { name: path.basename(full), path: full }, line: Number(number), column: 1 };
            });
            respond(request, { stackFrames: frames, totalFrames: frames.length });
            return;
        }
        case "scopes":
            respond(request, { scopes: [{ name: "Variables", variablesReference: (args.frameId || 0) + 1, expensive: false }] });
            return;
        case "variables":
            respond(request, { variables: await variables((args.variablesReference || 1) - 1) });
            return;
        case "evaluate": {
            const found = (await variables(args.frameId || 0)).find(variable => variable.name === String(args.expression).trim());
            if (found) respond(request, { result: found.value, type: found.type, variablesReference: 0 });
            else respond(request, {}, "There's no variable called " + args.expression + " here.");
            return;
        }
        case "continue":
            toProgram("continue");
            respond(request, { allThreadsContinued: true });
            return;
        case "next":
            toProgram("next");
            respond(request);
            return;
        case "stepIn":
            toProgram("step");
            respond(request);
            return;
        case "stepOut":
            toProgram("out");
            respond(request);
            return;
        case "pause":
            toProgram("pause");
            respond(request);
            return;
        case "disconnect":
        case "terminate":
            if (child && child.exitCode === null) child.kill();
            respond(request);
            if (request.command === "disconnect") setTimeout(() => process.exit(0), 100);
            return;
        default:
            respond(request);
    }
}

function launch(request, args) {
    const program = path.resolve(String(args.program || ""));
    if (!program.toLowerCase().endsWith(".jamo")) {
        respond(request, {}, "Pick a .jamo file to debug.");
        return;
    }
    root = path.dirname(program);
    stopOnEntry = Boolean(args.stopOnEntry);
    const jamo = args.jamoPath || "jamo";
    execFile(jamo, ["build", "--debug", program], { cwd: root }, (problem, _out, errors) => {
        if (errors) output(errors, "stderr");
        if (problem) {
            respond(request, {}, problem.code === "ENOENT" ? "VS Code can't find jamo.exe. Install Jamo, or set jamo.path in the settings." : "Jamo couldn't build " + path.basename(program) + ". The mistakes are in the Debug Console.");
            return;
        }
        const pipe = "\\\\.\\pipe\\jamo-vscode-" + process.pid + "-" + Date.now();
        const server = net.createServer(link => {
            socket = link;
            link.setEncoding("utf8");
            link.on("data", text => {
                fromProgram += text;
                let at;
                while ((at = fromProgram.indexOf("\n")) >= 0) {
                    heard(fromProgram.slice(0, at).replace(/\r$/, ""));
                    fromProgram = fromProgram.slice(at + 1);
                }
            });
            link.on("error", () => {});
            link.on("close", () => {
                while (questions.length > 0) questions.shift().done([]);
            });
        });
        server.listen(pipe, () => {
            child = spawn(program.slice(0, -5) + ".exe", (args.args || []).map(String), { cwd: root, env: { ...process.env, JAMO_DEBUG_PIPE: pipe } });
            child.stdout.on("data", text => output(text.toString(), "stdout"));
            child.stderr.on("data", text => output(text.toString(), "stderr"));
            child.on("error", problem => output("The program wouldn't start: " + problem.message + "\n", "stderr"));
            child.on("exit", code => {
                server.close();
                tell("exited", { exitCode: code === null ? 1 : code });
                tell("terminated");
            });
            respond(request);
            tell("initialized");
        });
    });
}
