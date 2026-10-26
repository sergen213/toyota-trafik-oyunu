// Apply "0xADDR Name" lines from a file as function names (creating functions if needed).
// Args: <names.txt>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Paths;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String line : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 2 || !p[0].startsWith("0x")) continue;
            Address a = toAddr(Long.parseLong(p[0].substring(2), 16));
            Function f = getFunctionAt(a);
            if (f == null) {
                disassemble(a);
                f = createFunction(a, null);
            }
            if (f != null) f.setName(p[1].replaceAll("[^A-Za-z0-9_.]", "_"), SourceType.USER_DEFINED);
        }
    }
}
