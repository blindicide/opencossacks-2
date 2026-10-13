// Отчёт о покрытии кода функциями в исполняемых блоках памяти.
// Использование: analyzeHeadless ... -process engine.exe -readOnly -noanalysis
//   -postScript Coverage.java <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import java.io.*;

public class Coverage extends GhidraScript {
    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressSet exec = new AddressSet();
        for (MemoryBlock b : currentProgram.getMemory().getBlocks())
            if (b.isExecute() && b.isInitialized()) exec.add(b.getStart(), b.getEnd());

        AddressSet inFuncs = new AddressSet();
        int funcs = 0, thunks = 0, unknownCc = 0, badStack = 0;
        for (Function f : fm.getFunctions(true)) {
            funcs++;
            if (f.isThunk()) thunks++;
            if (f.getCallingConventionName().equals(Function.UNKNOWN_CALLING_CONVENTION_STRING)) unknownCc++;
            if (f.getStackPurgeSize() == Function.INVALID_STACK_DEPTH_CHANGE
                || f.getStackPurgeSize() == Function.UNKNOWN_STACK_DEPTH_CHANGE) badStack++;
            inFuncs.add(f.getBody());
        }

        long instrBytes = 0, instrOutside = 0, dataBytes = 0;
        for (Instruction i : listing.getInstructions(exec, true)) {
            instrBytes += i.getLength();
            if (!inFuncs.contains(i.getAddress())) instrOutside += i.getLength();
        }
        for (Data d : listing.getDefinedData(exec, true)) dataBytes += d.getLength();
        long execBytes = exec.getNumAddresses();
        long undefined = execBytes - instrBytes - dataBytes;

        // Вызовы в никуда: ссылки типа CALL на адреса без функции.
        int orphanCalls = 0;
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (Address a : rm.getReferenceDestinationIterator(exec, true)) {
            if (fm.getFunctionAt(a) != null) continue;
            for (Reference r : rm.getReferencesTo(a))
                if (r.getReferenceType().isCall()) { orphanCalls++; break; }
        }

        String report = String.format(
            "исполняемых байт:           %,d%n" +
            "функций:                    %,d (переходников: %,d)%n" +
            "байт внутри функций:        %,d (%.1f%%)%n" +
            "байт инструкций:            %,d (%.1f%%)%n" +
            "  вне функций:              %,d%n" +
            "байт данных в коде:         %,d%n" +
            "неопределённых байт:        %,d (%.1f%%)%n" +
            "соглашение не определено:   %,d функций%n" +
            "очистка стека неизвестна:   %,d функций%n" +
            "вызовов в адреса без функции: %,d%n",
            execBytes, funcs, thunks,
            inFuncs.getNumAddresses(), 100.0 * inFuncs.getNumAddresses() / execBytes,
            instrBytes, 100.0 * instrBytes / execBytes, instrOutside, dataBytes,
            undefined, 100.0 * undefined / execBytes, unknownCc, badStack, orphanCalls);
        println(report);
        try (PrintWriter w = new PrintWriter(new File(getScriptArgs()[0]))) { w.print(report); }
    }
}
