// Выборочный доанализ после основного прогона (каждый этап замеряется отдельно):
//  1) функции с явным телом для обёрток, конфликтующих с переходниками компоновщика;
//  2) функции в начале каждого блока инструкций вне функций;
//  3) повторная очистка стека (X86 Function Callee Purge) только для функций,
//     где она осталась неизвестной.
// Использование: analyzeHeadless ... -process engine.exe -noanalysis
//   -postScript SelectiveFix.java <конфликты.txt> <отчёт.txt>
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.plugin.core.analysis.AutoAnalysisManager;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class SelectiveFix extends GhidraScript {
    private PrintWriter report;

    private void log(String s) {
        println(s);
        report.println(s);
        report.flush();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        report = new PrintWriter(new FileWriter(args[1]));
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        // 1) Обёртки вида «push; call; jmp [импорт]»: тело — от входа до первого перехода/возврата.
        long t0 = System.currentTimeMillis();
        int fixed = 0, failed = 0;
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            if (line.isBlank()) continue;
            Address entry = toAddr(line.split("\\s+")[0]);
            if (fm.getFunctionAt(entry) != null) { fixed++; continue; }
            if (listing.getInstructionAt(entry) == null) disassemble(entry);
            AddressSet body = new AddressSet();
            Instruction ins = listing.getInstructionAt(entry);
            while (ins != null) {
                body.add(ins.getMinAddress(), ins.getMaxAddress());
                FlowType ft = ins.getFlowType();
                if (ft.isJump() || ft.isTerminal()) {
                    if (ft.isJump() && ft.isComputed()) ins.setFlowOverride(FlowOverride.CALL_RETURN);
                    break;
                }
                ins = ins.getNext();
            }
            CreateFunctionCmd cmd = new CreateFunctionCmd(null, entry, body, SourceType.ANALYSIS);
            if (cmd.applyTo(currentProgram, monitor)) fixed++;
            else { failed++; log("  не удалось: " + entry + " — " + cmd.getStatusMsg()); }
        }
        log(String.format("этап 1: обёртки с переходниками — создано %d, ошибок %d, %.1f с",
            fixed, failed, (System.currentTimeMillis() - t0) / 1000.0));

        // 2) Инструкции вне функций: функция в начале каждого непрерывного блока.
        t0 = System.currentTimeMillis();
        AddressSet exec = new AddressSet();
        for (MemoryBlock b : currentProgram.getMemory().getBlocks())
            if (b.isExecute() && b.isInitialized()) exec.add(b.getStart(), b.getEnd());
        List<Address> starts = new ArrayList<>();
        Address prevEnd = null;
        for (Instruction i : listing.getInstructions(exec, true)) {
            if (fm.getFunctionContaining(i.getAddress()) != null) { prevEnd = null; continue; }
            if (prevEnd == null || !i.getAddress().equals(prevEnd.next())) starts.add(i.getAddress());
            prevEnd = i.getMaxAddress();
        }
        int created = 0, skipped = 0;
        for (Address a : starts) {
            if (monitor.isCancelled()) break;
            if (fm.getFunctionContaining(a) != null) { skipped++; continue; } // поглощена предыдущей
            CreateFunctionCmd cmd = new CreateFunctionCmd(a);
            if (cmd.applyTo(currentProgram, monitor)) created++; else skipped++;
        }
        log(String.format("этап 2: блоков вне функций %d — создано функций %d, пропущено %d, %.1f с",
            starts.size(), created, skipped, (System.currentTimeMillis() - t0) / 1000.0));

        // 3) Очистка стека: только функции с неизвестным значением (включая созданные выше).
        t0 = System.currentTimeMillis();
        AddressSet targets = new AddressSet();
        for (Function f : fm.getFunctions(true)) {
            int p = f.getStackPurgeSize();
            if (p == Function.UNKNOWN_STACK_DEPTH_CHANGE || p == Function.INVALID_STACK_DEPTH_CHANGE)
                targets.add(f.getEntryPoint());
        }
        long before = targets.getNumAddresses();
        AutoAnalysisManager mgr = AutoAnalysisManager.getAnalysisManager(currentProgram);
        mgr.scheduleOneTimeAnalysis(mgr.getAnalyzer("X86 Function Callee Purge"), targets);
        mgr.startAnalysis(monitor);
        int still = 0;
        for (Function f : fm.getFunctions(true)) {
            int p = f.getStackPurgeSize();
            if (p == Function.UNKNOWN_STACK_DEPTH_CHANGE || p == Function.INVALID_STACK_DEPTH_CHANGE) still++;
        }
        log(String.format("этап 3: очистка стека — целей %d, осталось неизвестных %d, %.1f с",
            before, still, (System.currentTimeMillis() - t0) / 1000.0));
        report.close();
    }
}
