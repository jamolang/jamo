// Jamo for VS Code: red and yellow underlines from jamo check, plus hover, go to definition, an outline and
// suggestions. The last four read the code itself, so they work while a file has mistakes and need no jamo.exe.
const { execFile } = require("child_process");
const fs = require("fs");
const path = require("path");

const CHECK_TIME_LIMIT_MS = 10000;
const MESSAGE_LINE = /^(.+?) line (\d+): (Warning: )?(.*)$/;
const MARKER_LINE = /^    ( *)(\^+)$/;
// The installed extension carries the libraries; run from Jamo's own folder, it reads them from there
const LIBRARY_FOLDER = fs.existsSync(path.join(__dirname, "libraries")) ? path.join(__dirname, "libraries") : path.join(__dirname, "..", "libraries");

// --- Underlines ---

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

// --- Reading definitions out of Jamo code ---

const NAME = "[A-Za-z_][A-Za-z0-9_]*";
const FUNC_LINE = new RegExp(`^func\\s+(?:(${NAME})(?:<[^>]*>)?\\.)?(${NAME})\\s*(<[^>]*>)?\\s*\\(([^)]*)\\)\\s*(?::\\s*(.+?))?\\s*(#.*)?$`);
const EXTERN_LINE = new RegExp(`^extern\\s+"[^"]*"\\s+func\\s+(${NAME})\\s*\\(([^)]*)\\)`);
const STRUCT_LINE = new RegExp(`^struct\\s+(${NAME})(<[^>]*>)?\\s*(#.*)?$`);
const ENUM_LINE = new RegExp(`^enum\\s+(${NAME})\\s*(#.*)?$`);
const TOP_LINE = new RegExp(`^(const|var)\\s+(${NAME})\\b(.*)$`);
const LOCAL_VAR = new RegExp(`^\\s+(?:var|const)\\s+(${NAME})\\b(.*)$`);
const FOR_LINE = new RegExp(`^\\s+for\\s+(${NAME})\\s+in\\b`);
const FIELD_LINE = new RegExp(`^\\s+(${NAME})\\s*:\\s*(.+?)\\s*(#.*)?$`);
const MEMBER_LINE = new RegExp(`^\\s+(${NAME})\\s*(#.*)?$`);
const USE_LINE = /^use\s+"([^"]+)"/;

// The # lines straight above a line, without their #
function comment_above(lines, at) {
    const words = [];
    for (let i = at - 1; i >= 0 && /^\s*#/.test(lines[i]); i--) words.unshift(lines[i].replace(/^\s*#\s?/, ""));
    return words.join("\n");
}

// Every name a file makes: { name, kind, owner, line, column, signature, doc, file, from, to }.
// Locals (inputs and vars inside a function) also have from and to: the lines where they can be used
function index_source(text, file) {
    const lines = text.split(/\r?\n/);
    const found = [];
    const uses = [];
    let block = null;          // the struct or enum being read
    let func = null;           // the function being read
    const add = (item) => found.push({ file, owner: "", doc: "", ...item });
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const use = USE_LINE.exec(line);
        if (use) uses.push(use[1]);
        if (/^end\b/.test(line)) {
            if (func) for (const item of found) if (item.func === func) item.to = i;
            block = null;
            func = null;
            continue;
        }
        let m;
        if ((m = FUNC_LINE.exec(line))) {
            const [, owner, name, types, inputs, gives] = m;
            func = { line: i };
            const signature = `func ${owner ? owner + "." : ""}${name}${types || ""}(${inputs.trim()})${gives ? ": " + gives : ""}`;
            add({ name, kind: owner ? "method" : "function", owner: owner || "", line: i, column: line.indexOf(name, 5), signature, doc: comment_above(lines, i) });
            let column = line.indexOf("(") + 1;
            for (const part of inputs.split(",")) {
                const input = new RegExp(`^\\s*(${NAME})\\s*:\\s*(.+?)\\s*$`).exec(part);
                if (input) add({ name: input[1], kind: "input", line: i, column: line.indexOf(input[1], column), signature: `${input[1]}: ${input[2]}`, func, from: i, to: lines.length });
                column += part.length + 1;
            }
            if (owner) add({ name: "self", kind: "input", line: i, column: 5, signature: `self: ${owner}`, func, from: i, to: lines.length });
            continue;
        }
        if ((m = EXTERN_LINE.exec(line))) {
            add({ name: m[1], kind: "function", line: i, column: line.indexOf(m[1], 7), signature: line.trim(), doc: comment_above(lines, i) });
            continue;
        }
        if ((m = STRUCT_LINE.exec(line))) {
            block = { kind: "struct", name: m[1] };
            add({ name: m[1], kind: "struct", line: i, column: 7, signature: `struct ${m[1]}${m[2] || ""}`, doc: comment_above(lines, i) });
            continue;
        }
        if ((m = ENUM_LINE.exec(line))) {
            block = { kind: "enum", name: m[1] };
            add({ name: m[1], kind: "enum", line: i, column: 5, signature: `enum ${m[1]}`, doc: comment_above(lines, i) });
            continue;
        }
        if ((m = TOP_LINE.exec(line))) {
            add({ name: m[2], kind: m[1] === "const" ? "constant" : "variable", line: i, column: line.indexOf(m[2]), signature: line.replace(/\s*#.*$/, "").trim(), doc: comment_above(lines, i) });
            continue;
        }
        if (block && block.kind === "struct" && (m = FIELD_LINE.exec(line))) {
            add({ name: m[1], kind: "field", owner: block.name, line: i, column: line.indexOf(m[1]), signature: `${block.name}.${m[1]}: ${m[2]}`, doc: (m[3] || "").replace(/^#\s?/, "") });
            continue;
        }
        if (block && block.kind === "enum" && (m = MEMBER_LINE.exec(line))) {
            add({ name: m[1], kind: "member", owner: block.name, line: i, column: line.indexOf(m[1]), signature: `${block.name}.${m[1]}`, doc: (m[2] || "").replace(/^#\s?/, "") });
            continue;
        }
        if (func && ((m = LOCAL_VAR.exec(line)) || (m = FOR_LINE.exec(line)))) {
            const rest = (m[2] || "").replace(/\s*#.*$/, "");
            const signature = FOR_LINE.test(line) ? `for ${m[1]} in ...` : `var ${m[1]}${rest}`;
            add({ name: m[1], kind: "variable", line: i, column: line.indexOf(m[1], line.search(/\S/)), signature: signature.trim(), func, from: i, to: lines.length });
        }
    }
    for (const item of found) delete item.func;
    return { items: found, uses };
}

// The name under the cursor, and whether a dot comes straight before it (so it's a field, method or enum member)
function word_at(line_text, column) {
    const pattern = new RegExp(NAME, "g");
    let m;
    while ((m = pattern.exec(line_text))) {
        if (column >= m.index && column <= m.index + m[0].length) {
            if (/^[0-9]/.test(m[0])) return null;
            return { word: m[0], start: m.index, end: m.index + m[0].length, after_dot: line_text[m.index - 1] === "." };
        }
    }
    return null;
}

// Where a name is made, best match first. Locals in scope beat everything; after a dot only fields, methods and members count
function find_definitions(items, word, file, line, after_dot) {
    const named = items.filter((item) => item.name === word);
    if (after_dot) return named.filter((item) => item.kind === "field" || item.kind === "method" || item.kind === "member");
    const locals = named.filter((item) => item.from !== undefined && item.file === file && item.from <= line && line <= item.to);
    if (locals.length) return [locals.reduce((best, item) => (item.from >= best.from ? item : best))];
    const tops = named.filter((item) => item.from === undefined && item.kind !== "field" && item.kind !== "method" && item.kind !== "member");
    const here = tops.filter((item) => item.file === file);
    return here.length ? here : tops;
}

// --- Built-in functions, for hover ---
const BUILT_IN = {
    print: "print(x)\nPrints any value and a line ending.",
    len: "len(x): int\nHow many items an array has, or how many bytes are in text.",
    int: "int(x): int\nA float without its decimals (toward zero), or text with digits. Bad text stops with a run error.",
    float: "float(x): float\nAn int as a float, or text like 2.5.",
    string: "string(x): string\nAn int, float or bool as text.",
    bool: "bool",
    input: "input(): string\nReads a line that's typed, without its line ending. Gives \"\" when there's no more.",
    args: "args(): [string]\nThe command line's inputs, without the program's own name.",
    read_file: "read_file(path): string\nA file's text. Stops with a run error if it can't.",
    write_file: "write_file(path, text)\nReplaces a file. Stops with a run error if it can't.",
    exists: "exists(path): bool\nTrue if a file or folder is there.",
    fail: "fail(message)\nStops the program with a run error.",
    byte: "byte(text, i): int\nThe number of the byte at place i, from 0 to 255.",
    hash_of: "hash_of(x): int\nA number from an int, string, bool or enum that's never below 0 and always the same for the same value.",
};
const KEYWORDS = ["func", "end", "var", "const", "if", "elif", "else", "while", "for", "in", "break", "continue", "return", "and", "or", "not", "struct", "enum", "match", "case", "use", "try", "catch", "true", "false"];

// --- Files a program uses: libraries come with the extension, other files sit beside the program ---
function resolve_use(name, from_file) {
    const library = path.join(LIBRARY_FOLDER, name);
    if (fs.existsSync(library)) return library;              // like jamo.exe, the built-in libraries win
    return path.resolve(path.dirname(from_file), name);
}

// Every item in a file and the files it uses, following uses through. read(file) gives the text or null
function gather(file, read) {
    const seen = new Set();
    const items = [];
    const visit = (current) => {
        const key = path.resolve(current).toLowerCase();
        if (seen.has(key) || seen.size > 200) return;
        seen.add(key);
        const text = read(current);
        if (text === null) return;
        const index = index_source(text, current);
        items.push(...index.items);
        for (const name of index.uses) visit(resolve_use(name, current));
    };
    visit(file);
    return items;
}

function hover_text(item) {
    const where = item.file.startsWith(LIBRARY_FOLDER) ? path.basename(item.file) : "";
    let text = "```jamo\n" + item.signature + "\n```";
    if (item.doc) text += "\n\n" + item.doc;
    text += `\n\n*${item.kind}${where ? " in " + where : ""}*`;
    return text;
}

// --- VS Code ---
function activate(context) {
    const vscode = require("vscode");
    const underlines = vscode.languages.createDiagnosticCollection("jamo");
    context.subscriptions.push(underlines);

    const check = (document) => {
        if (document.languageId !== "jamo" || document.uri.scheme !== "file") return;
        if (document.uri.fsPath.startsWith(LIBRARY_FOLDER)) return;
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

    // Open editors give their unsaved text; other files are read from disk
    const read = (file) => {
        const open = vscode.workspace.textDocuments.find((d) => d.uri.scheme === "file" && path.resolve(d.uri.fsPath).toLowerCase() === path.resolve(file).toLowerCase());
        if (open) return open.getText();
        try { return fs.readFileSync(file, "utf8"); } catch { return null; }
    };
    const lookup = (document, position) => {
        const found = word_at(document.lineAt(position.line).text, position.character);
        if (!found) return null;
        const items = gather(document.uri.fsPath, read);
        return { found, matches: find_definitions(items, found.word, document.uri.fsPath, position.line, found.after_dot) };
    };
    const selector = { language: "jamo" };
    const KINDS = {
        function: vscode.SymbolKind.Function, method: vscode.SymbolKind.Method, struct: vscode.SymbolKind.Struct,
        enum: vscode.SymbolKind.Enum, constant: vscode.SymbolKind.Constant, variable: vscode.SymbolKind.Variable,
        field: vscode.SymbolKind.Field, member: vscode.SymbolKind.EnumMember, input: vscode.SymbolKind.Variable,
    };
    const COMPLETE = {
        function: vscode.CompletionItemKind.Function, method: vscode.CompletionItemKind.Method, struct: vscode.CompletionItemKind.Struct,
        enum: vscode.CompletionItemKind.Enum, constant: vscode.CompletionItemKind.Constant, variable: vscode.CompletionItemKind.Variable,
        field: vscode.CompletionItemKind.Field, member: vscode.CompletionItemKind.EnumMember, input: vscode.CompletionItemKind.Variable,
    };

    context.subscriptions.push(vscode.languages.registerDefinitionProvider(selector, {
        provideDefinition(document, position) {
            const result = lookup(document, position);
            if (!result) return null;
            return result.matches.map((item) => new vscode.Location(vscode.Uri.file(item.file), new vscode.Position(item.line, Math.max(0, item.column))));
        },
    }));

    context.subscriptions.push(vscode.languages.registerHoverProvider(selector, {
        provideHover(document, position) {
            const result = lookup(document, position);
            if (!result) return null;
            const range = new vscode.Range(position.line, result.found.start, position.line, result.found.end);
            if (!result.matches.length) {
                const built_in = BUILT_IN[result.found.word];
                if (!built_in || result.found.after_dot) return null;
                const [first, ...rest] = built_in.split("\n");
                return new vscode.Hover(new vscode.MarkdownString("```jamo\n" + first + "\n```" + (rest.length ? "\n\n" + rest.join("\n") : "") + "\n\n*built in*"), range);
            }
            return new vscode.Hover(result.matches.slice(0, 4).map((item) => new vscode.MarkdownString(hover_text(item))), range);
        },
    }));

    context.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(selector, {
        provideDocumentSymbols(document) {
            const { items } = index_source(document.getText(), document.uri.fsPath);
            const symbols = [];
            const owners = new Map();
            for (const item of items) {
                if (item.from !== undefined) continue;
                const range = new vscode.Range(item.line, 0, item.line, document.lineAt(item.line).text.length);
                const symbol = new vscode.DocumentSymbol(item.owner && item.kind !== "method" ? item.name : (item.owner ? `${item.owner}.${item.name}` : item.name), item.signature, KINDS[item.kind], range, range);
                if ((item.kind === "field" || item.kind === "member") && owners.has(item.owner)) owners.get(item.owner).children.push(symbol);
                else symbols.push(symbol);
                if (item.kind === "struct" || item.kind === "enum") owners.set(item.name, symbol);
            }
            return symbols;
        },
    }));

    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(selector, {
        provideCompletionItems(document, position) {
            const before = document.lineAt(position.line).text.slice(0, position.character);
            const after_dot = /\.[A-Za-z0-9_]*$/.test(before);
            const items = gather(document.uri.fsPath, read);
            const out = new Map();
            for (const item of items) {
                const member = item.kind === "field" || item.kind === "method" || item.kind === "member";
                if (after_dot !== member) continue;
                if (item.from !== undefined && (item.file !== document.uri.fsPath || item.from > position.line || position.line > item.to)) continue;
                if (item.name.startsWith("_") || out.has(item.name + item.kind)) continue;
                const completion = new vscode.CompletionItem(item.name, COMPLETE[item.kind]);
                completion.detail = item.signature;
                if (item.doc) completion.documentation = new vscode.MarkdownString(item.doc);
                out.set(item.name + item.kind, completion);
            }
            if (!after_dot) {
                for (const word of KEYWORDS) out.set(word + "keyword", new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword));
                for (const word of Object.keys(BUILT_IN)) if (!out.has(word + "function")) {
                    const completion = new vscode.CompletionItem(word, vscode.CompletionItemKind.Function);
                    completion.detail = BUILT_IN[word].split("\n")[0];
                    out.set(word + "function", completion);
                }
            }
            return [...out.values()];
        },
    }, "."));
}

function deactivate() {}

module.exports = { activate, deactivate, read_check_output, index_source, word_at, find_definitions, gather, resolve_use };
