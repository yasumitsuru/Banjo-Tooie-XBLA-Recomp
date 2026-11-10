// DumpXex.java — Ghidra headless postScript (no PyGhidra needed).
// Dumps: program.txt (blocks), functions.txt (entry|name|size),
// refs_to_data.txt (code refs into non-executable blocks),
// call_edges.txt (code-to-code refs landing on function entries).
//@category Analysis
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.listing.Program;

public class DumpXex extends GhidraScript {

	private static String h(Address a) {
		return "0x" + Long.toHexString(a.getOffset());
	}

	@Override
	public void run() throws Exception {
		String outdir = getScriptArgs()[0];
		File od = new File(outdir);
		od.mkdirs();

		Program prog = getCurrentProgram();
		FunctionManager fm = prog.getFunctionManager();
		Listing listing = prog.getListing();
		ReferenceManager rm = prog.getReferenceManager();
		Memory mem = prog.getMemory();

		PrintWriter pw = new PrintWriter(new FileWriter(new File(od, "program.txt")));
		pw.println("name|" + prog.getName());
		pw.println("image_base|" + h(prog.getImageBase()));
		pw.println("language|" + prog.getLanguageID().getIdAsString());
		pw.println("compiler|" + prog.getCompilerSpec().getCompilerSpecID().getIdAsString());
		for (MemoryBlock blk : mem.getBlocks()) {
			pw.println("block|" + blk.getName() + "|" + h(blk.getStart()) + "|" + h(blk.getEnd())
					+ "|exec=" + blk.isExecute() + "|write=" + blk.isWrite() + "|read=" + blk.isRead());
		}
		pw.close();
		println("program.txt done");

		pw = new PrintWriter(new FileWriter(new File(od, "functions.txt")));
		int n = 0;
		for (Function fn : fm.getFunctions(true)) {
			pw.println(h(fn.getEntryPoint()) + "|" + fn.getName() + "|"
					+ fn.getBody().getNumAddresses());
			n++;
		}
		pw.close();
		println("functions: " + n);

		List<MemoryBlock> dataBlocks = new ArrayList<MemoryBlock>();
		for (MemoryBlock blk : mem.getBlocks()) {
			if (!blk.isExecute()) {
				dataBlocks.add(blk);
			}
		}

		pw = new PrintWriter(new FileWriter(new File(od, "refs_to_data.txt")));
		PrintWriter pw2 = new PrintWriter(new FileWriter(new File(od, "call_edges.txt")));
		int nd = 0;
		int nc = 0;
		for (Instruction ins : listing.getInstructions(true)) {
			Address from = ins.getAddress();
			Function ffn = fm.getFunctionContaining(from);
			String fnName = ffn == null ? "" : ffn.getName();
			for (Reference ref : rm.getReferencesFrom(from)) {
				Address to = ref.getToAddress();
				boolean data = false;
				for (MemoryBlock b : dataBlocks) {
					if (b.contains(to)) {
						data = true;
						break;
					}
				}
				if (data) {
					pw.println(h(from) + "|" + fnName + "|" + h(to) + "|"
							+ ref.getReferenceType().getName());
					nd++;
				}
				else {
					Function tfn = fm.getFunctionAt(to);
					if (tfn != null) {
						pw2.println(fnName + "|" + h(from) + "|" + tfn.getName() + "|" + h(to));
						nc++;
					}
				}
			}
		}
		pw.close();
		pw2.close();
		println("refs_to_data: " + nd + "  call_edges: " + nc);
		println("DUMP_OK");
	}
}
