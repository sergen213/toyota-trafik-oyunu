// Decompile every function whose entry lies in [start,end) and write C to <out>.
// Args: <out.c> <startHex> <endHex>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class DecompileRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address start = toAddr(Long.parseLong(a[1], 16));
        Address end = toAddr(Long.parseLong(a[2], 16));
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(a[0], "UTF-8")) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(start, true)) {
                if (f.getEntryPoint().compareTo(end) >= 0) break;
                DecompileResults r = d.decompileFunction(f, 120, monitor);
                w.println("// ==== " + f.getEntryPoint() + " " + f.getName());
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// FAILED " + r.getErrorMessage());
            }
        }
    }
}
