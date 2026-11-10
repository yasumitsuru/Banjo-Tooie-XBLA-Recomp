// DecompileAt.java - Ghidra headless postScript (MISSAO 02).
// Decompila as funcoes que contem os enderecos dados e grava <saida>/<endereco>.c.
// Uso: -postScript DecompileAt.java <pasta_saida> <end1> [end2 ...]   (enderecos em hex)
//@category Analysis
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		File out = new File(args[0]);
		out.mkdirs();
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		for (int i = 1; i < args.length; i++) {
			String a = args[i].toLowerCase().replace("0x", "");
			Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseLong(a, 16));
			Function f = getFunctionContaining(addr);
			File file = new File(out, "0x" + a + ".c");
			try (PrintWriter pw = new PrintWriter(new FileWriter(file))) {
				if (f == null) {
					pw.println("// sem funcao em 0x" + a);
					continue;
				}
				DecompileResults r = di.decompileFunction(f, 120, monitor);
				pw.println("// " + f.getName() + " @ " + f.getEntryPoint() + " (pedido 0x" + a + ")");
				if (r != null && r.getDecompiledFunction() != null) {
					pw.println(r.getDecompiledFunction().getC());
				} else {
					pw.println("// falha: " + (r == null ? "null" : r.getErrorMessage()));
				}
			}
		}
		di.dispose();
	}
}
