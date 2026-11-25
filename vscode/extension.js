// Red and yellow underlines, from running jamo check whenever a Jamo file is opened or saved.
const { execFile } = require("child_process");
const path = require("path");

const CHECK_TIME_LIMIT_MS = 10000;
const MESSAGE_LINE = /^(.+?) line (\d+): (Warning: )?(.*)$/;
const MARKER_LINE = /^    ( *)(\^+)$/;

// Turns what jamo check prints into a list of { file, line, column, length, words, hint, is_warning }
function read_check_output(text) {
    const found = [];
    const lines = text.split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
        const message = MESSAGE_LINE.exec(lines[i]);
        if (!message) continue;
        const item = { file: message[1], line: Number(message[2]) - 1, column: 0, length: 1, words: message[4], hint: "", is_warning: Boolean(message[3]) };
        const marker = MARKER_LINE.exec(lines[i + 2] || "");
        if (marker) {
            item.column = marker[1].length;
            item.length = marker[2].length;
            i += 2;
        }
        const next = lines[i + 1] || "";
        if (next && !MESSAGE_LINE.test(next)) {
            item.hint = next;
            i += 1;
        }
        found.push(item);
    }
    return found;
}

function activate(context) {
    const vscode = require("vscode");
    const underlines = vscode.languages.createDiagnosticCollection("jamo");
    context.subscriptions.push(underlines);

    const check = (document) => {
        if (document.languageId !== "jamo" || document.uri.scheme !== "file") return;
        const jamo = vscode.workspace.getConfiguration("jamo").get("path");
        const folder = path.dirname(document.uri.fsPath);
        execFile(jamo, ["check", document.uri.fsPath], { cwd: folder, timeout: CHECK_TIME_LIMIT_MS }, (problem, _out, err) => {
            if (problem && problem.code === "ENOENT") {
                vscode.window.showWarningMessage(`Jamo: I can't find ${jamo}. Set jamo.path in your settings.`);
                return;
            }
            const by_file = new Map();
            for (const item of read_check_output(err)) {
                const file = vscode.Uri.file(path.resolve(folder, item.file)).toString();
                const range = new vscode.Range(item.line, item.column, item.line, item.column + item.length);
                const words = item.hint ? `${item.words}\n${item.hint}` : item.words;
                const severity = item.is_warning ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error;
                if (!by_file.has(file)) by_file.set(file, []);
                by_file.get(file).push(new vscode.Diagnostic(range, words, severity));
            }
            underlines.clear();
            for (const [file, items] of by_file) underlines.set(vscode.Uri.parse(file), items);
        });
    };

    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(check));
    context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(check));
    vscode.workspace.textDocuments.forEach(check);
}

function deactivate() {}

module.exports = { activate, deactivate, read_check_output };
