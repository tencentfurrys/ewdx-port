// ModLauncherActivity.java - front-end / mod manager for the EchidnaWarsDX port.
//
// WHAT A "MOD" IS HERE
// --------------------
// The decompiled script resolves every asset as  data\<pool>\<name><ext>  with
// pool in {map, mold, mot, music, pic, se} (GUIDE.md section 8, the dir_c==0
// branch of all six loaders). So a mod is nothing more than a bag of
// replacement asset files. The real mod packs for this game look like:
//
//   MOT Presets REQUIRED/  ->  b_urb_c.mot, b_urb2.mot, ...   (data/mot/)
//   KaraSkulldog/Skin/     ->  p100.bmp .. p107.bmp          (data/pic/)
//   decorative vore/       ->  molds/o42_delta_1.mol         (data/mold/)
//   Vivian/Covered/Chubby/ ->  m_mon17.bmp                   (data/pic/)
//
// Two things follow, and they drive the whole design:
//
// 1) The mod's FOLDER names are arbitrary and must be ignored. "Required MOTS"
//    is not the pool "mot". Files are therefore placed by EXTENSION, using the
//    table below. Anything else (.txt readmes, .anims.txt) is kept alongside
//    the mod for reference but never copied into data/.
//
// 2) Many packs ship several VARIANTS of the same filename (Vivian alone has
//    ~331 entries, mostly alternative m_mon17.bmp). A pack is therefore
//    presented as: for each distinct filename, pick one variant. Filenames
//    with a single variant are applied automatically.
//
// HOW APPLICATION WORKS (no native changes required)
// -------------------------------------------------
// Everything already runs out of <filesDir>/data. Activating a mod just copies
// its files over data/, remembering the pristine bytes of anything it
// overwrites, so it can always be taken back off again:
//
//   <filesDir>/data/...            live tree the game reads
//   <filesDir>/mods/<id>/payload/  an installed mod's files
//   <filesDir>/.modstate/backup/   pristine copies of overwritten files
//   <filesDir>/.modstate/active    id of the active mod ("" = vanilla)
//
// Mods only ever ADD or OVERWRITE - they never delete - so "reset to vanilla"
// is just "copy the backup back", which keeps this safe and idempotent.
//
// FIRST-LAUNCH ORDERING
// ---------------------
// The native bootstrap (ewdx_boot_bootstrap) unpacks assets/data/ into
// filesDir exactly once, gated on a data/.ewdx_ok marker. If a mod were applied
// before that first unpack, the unpack would overwrite it. So this Activity
// stages data/ itself, from the APK's own assets, BEFORE offering any mod -
// after which the native bootstrap sees the marker and stays out of the way.
// (start.ax and save.dat are deliberately left to the native side: start.ax
// needs its Shift-JIS-safe '\\'->'/' DS patch, which only the native path does.)

package net.dgate.ewdx;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;

public class ModLauncherActivity extends Activity {

    // ---- asset extension -> data/ pool (the script's own directory names) ----
    private static final Map<String, String> POOL_OF_EXT = new LinkedHashMap<>();
    static {
        POOL_OF_EXT.put("mot", "mot");     // animation data
        POOL_OF_EXT.put("mol", "mold");    // model data
        POOL_OF_EXT.put("map", "map");     // stage data
        POOL_OF_EXT.put("bmp", "pic");     // sprites / atlases
        POOL_OF_EXT.put("wav", "se");      // sound effects
        POOL_OF_EXT.put("ogg", "music");   // background music
    }

    private static final String TAG_DATA_MARK = "data/.ewdx_ok";

    private File filesDir;
    private File dataDir;      // <filesDir>/data
    private File modsDir;      // <filesDir>/mods
    private File stateDir;     // <filesDir>/.modstate
    private File backupDir;    // <filesDir>/.modstate/backup
    private File activeFile;   // <filesDir>/.modstate/active

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final List<Mod> mods = new ArrayList<>();

    // variant choices: filename -> chosen variant index (per selected mod)
    private final Map<String, Integer> variantChoice = new LinkedHashMap<>();
    private Mod selected;
    private int selectedIndex = 0; // 0 == vanilla

    private TextView status;
    private LinearLayout variantBox;
    private LinearLayout modListBox;
    private Button btnPlay, btnReset, btnImport, btnDelete;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        filesDir = getFilesDir();
        dataDir = new File(filesDir, "data");
        modsDir = new File(filesDir, "mods");
        stateDir = new File(filesDir, ".modstate");
        backupDir = new File(stateDir, "backup");
        activeFile = new File(stateDir, "active");
        modsDir.mkdirs();
        stateDir.mkdirs();

        setContentView(buildUi());
        // Staging is ~100 MB on a fresh install: keep it off the UI thread.
        new Thread(new Runnable() {
            @Override public void run() {
                final String res = stageBaseData();
                ui.post(new Runnable() {
                    @Override public void run() { onStaged(res); }
                });
            }
        }).start();
    }

    // ------------------------------------------------------------------ UI ----

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (12 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("EchidnaWarsDX — Mods");
        title.setTextSize(20);
        title.setPadding(0, 0, 0, 6);
        root.addView(title);

        status = new TextView(this);
        status.setText("Preparing…");
        status.setPadding(0, 0, 0, 8);
        root.addView(status);

        ScrollView scroll = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(col);

        modListBox = new LinearLayout(this);
        modListBox.setOrientation(LinearLayout.VERTICAL);
        col.addView(modListBox);

        variantBox = new LinearLayout(this);
        variantBox.setOrientation(LinearLayout.VERTICAL);
        variantBox.setPadding(0, 10, 0, 0);
        col.addView(variantBox);

        root.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        btnPlay = mkButton("Apply mod & Play");
        btnPlay.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doApplyAndPlay(); }
        });
        root.addView(btnPlay);

        btnReset = mkButton("Reset to Vanilla");
        btnReset.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doReset(); }
        });
        root.addView(btnReset);

        btnImport = mkButton("Import mod (.zip)…");
        btnImport.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doImport(); }
        });
        root.addView(btnImport);

        btnDelete = mkButton("Delete selected mod");
        btnDelete.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { doDelete(); }
        });
        root.addView(btnDelete);

        TextView hint = new TextView(this);
        hint.setTextSize(11);
        hint.setPadding(0, 8, 0, 0);
        hint.setText("Mods live in " + modsDir.getAbsolutePath()
                + "\nInstall the app once so game data unpacks, then import a "
                + "mod zip here. A zip containing further zips imports each of "
                + "them as its own mod. .rar is not supported.");
        root.addView(hint);

        return root;
    }

    private Button mkButton(String s) {
        Button b = new Button(this);
        b.setText(s);
        return b;
    }

    private void onStaged(String res) {
        status.setText(res);
        refreshMods();
        setButtonsEnabled(true);
    }

    private void setButtonsEnabled(boolean on) {
        btnPlay.setEnabled(on);
        btnReset.setEnabled(on);
        btnImport.setEnabled(on);
        btnDelete.setEnabled(on);
    }

    // ------------------------------------------------------- base data stage ----

    /**
     * Copy assets/data/ -> filesDir/data/ on first run and write the same
     * marker the native bootstrap uses, so the native unpack stays skipped and
     * cannot clobber an applied mod. Idempotent.
     */
    private String stageBaseData() {
        File mark = new File(filesDir, TAG_DATA_MARK);
        if (mark.exists()) return "Game data present (" + countFiles(dataDir) + " files).";
        try {
            AssetManager am = getAssets();
            long[] n = new long[1];
            copyAssetTree(am, "data", dataDir, n);
            if (n[0] == 0) {
                return "No game data in this APK — reinstall the full APK.";
            }
            mark.getParentFile().mkdirs();
            FileOutputStream fo = new FileOutputStream(mark);
            fo.write("1\n".getBytes("UTF-8"));
            fo.close();
            return "Unpacked game data (" + n[0] + " files).";
        } catch (IOException e) {
            return "Failed to unpack game data: " + e.getMessage();
        }
    }

    private void copyAssetTree(AssetManager am, String assetPath, File dst, long[] n)
            throws IOException {
        String[] kids = am.list(assetPath);
        if (kids == null || kids.length == 0) return;
        for (String kid : kids) {
            String childAsset = assetPath + "/" + kid;
            File out = new File(dst, kid);
            // AssetManager.list() reports directories as entries too; probing
            // the nested list distinguishes them (a file lists null/empty).
            String[] grand = am.list(childAsset);
            if (grand != null && grand.length > 0) {
                out.mkdirs();
                copyAssetTree(am, childAsset, out, n);
            } else {
                copyAssetStream(am, childAsset, out);
                n[0]++;
            }
        }
    }

    private void copyAssetStream(AssetManager am, String asset, File dst)
            throws IOException {
        InputStream in = am.open(asset, AssetManager.ACCESS_STREAMING);
        File parent = dst.getParentFile();
        if (parent != null) parent.mkdirs();
        OutputStream out = new FileOutputStream(dst);
        try {
            byte[] buf = new byte[32768];
            int r;
            while ((r = in.read(buf)) > 0) out.write(buf, 0, r);
        } finally {
            try { in.close(); } catch (IOException ignored) { }
            out.close();
        }
    }

    // ------------------------------------------------------------ mod model ----

    private static class Mod {
        String id;                       // folder name under mods/
        String label;                    // display name
        File dir;
        File payload;
        // filename (e.g. b_urb_t2.mot) -> every variant found, as paths
        // relative to payload/
        Map<String, List<String>> files = new LinkedHashMap<>();
        int ambiguous;                   // how many filenames have >1 variant

        int assetCount() {
            int n = 0;
            for (List<String> v : files.values()) n += v.size();
            return n;
        }
    }

    /** Pool an asset extension belongs to, or null if it is not game data. */
    private static String poolOf(String fname) {
        int dot = fname.lastIndexOf('.');
        if (dot < 0 || dot == fname.length() - 1) return null;
        return POOL_OF_EXT.get(fname.substring(dot + 1).toLowerCase());
    }

    private void refreshMods() {
        mods.clear();
        File[] kids = modsDir.listFiles();
        List<File> dirs = new ArrayList<>();
        if (kids != null) {
            for (File f : kids) if (f.isDirectory()) dirs.add(f);
        }
        java.util.Collections.sort(dirs, new Comparator<File>() {
            @Override public int compare(File a, File b) {
                return a.getName().compareToIgnoreCase(b.getName());
            }
        });
        for (File d : dirs) {
            File payload = new File(d, "payload");
            if (!payload.isDirectory()) continue;
            Mod m = new Mod();
            m.id = d.getName();
            m.dir = d;
            m.payload = payload;
            m.label = prettyName(d);
            indexPayload(m);
            mods.add(m);
        }
        buildModList();
    }

    /** Folder name -> readable label ("vivian_simple_quick_setup" -> "Vivian simple quick setup"). */
    private static String prettyName(File d) {
        File nm = new File(d, "name.txt");
        if (nm.isFile()) {
            try {
                String s = readAll(nm).trim();
                if (s.length() > 0) return s;
            } catch (IOException ignored) { }
        }
        String s = d.getName().replace('_', ' ').trim();
        return s.isEmpty() ? d.getName() : s;
    }

    /** Walk payload/, grouping game-asset files by basename into variant lists. */
    private void indexPayload(Mod m) {
        List<File> all = new ArrayList<>();
        collectFiles(m.payload, all);
        java.util.Collections.sort(all, new Comparator<File>() {
            @Override public int compare(File a, File b) {
                return a.getAbsolutePath().compareToIgnoreCase(b.getAbsolutePath());
            }
        });
        for (File f : all) {
            String pool = poolOf(f.getName());
            if (pool == null) continue;   // .txt and friends: reference only
            String key = f.getName();
            String rel = relativize(m.payload, f);
            List<String> lst = m.files.get(key);
            if (lst == null) {
                lst = new ArrayList<>();
                m.files.put(key, lst);
            }
            lst.add(rel);
        }
        m.ambiguous = 0;
        for (List<String> v : m.files.values()) if (v.size() > 1) m.ambiguous++;
    }

    private static void collectFiles(File dir, List<File> out) {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        for (File f : kids) {
            if (f.isDirectory()) collectFiles(f, out);
            else out.add(f);
        }
    }

    private static String relativize(File base, File f) {
        String b = base.getAbsolutePath();
        String a = f.getAbsolutePath();
        if (a.startsWith(b)) {
            String r = a.substring(b.length());
            while (r.startsWith(File.separator)) r = r.substring(1);
            return r.replace(File.separatorChar, '/');
        }
        return f.getName();
    }

    /**
     * Shorten a variant path for display by dropping the directory prefix that
     * every variant of that file shares. Real packs bury the useful part deep
     * ("Vivian/Covered/Chubby/Belly Button/Round Bulge/m_mon17.bmp"), and with
     * 126 variants of one file the shared stem is pure noise in a spinner.
     */
    private static String shorten(List<String> variants, String path) {
        String[] parts = path.split("/");
        int keepFrom = parts.length - 1;                 // always keep the filename
        String[] first = variants.get(0).split("/");
        for (int d = 0; d < parts.length - 1 && d < first.length - 1; d++) {
            if (!parts[d].equals(first[d])) break;
            keepFrom = d + 1;
        }
        StringBuilder sb = new StringBuilder();
        for (int i = keepFrom; i < parts.length; i++) {
            if (sb.length() > 0) sb.append('/');
            sb.append(parts[i]);
        }
        return sb.toString();
    }

    // ------------------------------------------------------------ mod list UI ----

    private void buildModList() {
        modListBox.removeAllViews();
        variantChoice.clear();

        RadioGroup grp = new RadioGroup(this);
        grp.setOrientation(RadioGroup.VERTICAL);

        RadioButton vanilla = new RadioButton(this);
        vanilla.setText("Vanilla (no mod)");
        vanilla.setChecked(true);
        grp.addView(vanilla);

        for (int i = 0; i < mods.size(); i++) {
            final Mod m = mods.get(i);
            RadioButton rb = new RadioButton(this);
            rb.setText(m.label + "  [" + m.files.size() + " files"
                    + (m.ambiguous > 0 ? ", " + m.ambiguous + " choose" : "") + "]");
            grp.addView(rb);
        }

        grp.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup g, int checkedId) {
                selectedIndex = checkedId;          // id == child index
                selected = (selectedIndex <= 0) ? null : mods.get(selectedIndex - 1);
                buildVariantRows();
            }
        });

        modListBox.addView(grp);
        selectedIndex = 0;
        selected = null;
        buildVariantRows();

        btnDelete.setEnabled(selected != null);
    }

    /**
     * One row per ambiguous filename: label + spinner over that file's
     * variants (shown as their path inside the pack, which is how these packs
     * are actually organised - "Covered/Chubby/Round Bulge/m_mon17.bmp").
     */
    private void buildVariantRows() {
        variantBox.removeAllViews();
        variantChoice.clear();
        btnDelete.setEnabled(selected != null);
        if (selected == null || selected.ambiguous == 0) return;

        TextView head = new TextView(this);
        head.setText("Variants to use:");
        head.setPadding(0, 8, 0, 2);
        variantBox.addView(head);

        for (final Map.Entry<String, List<String>> e : selected.files.entrySet()) {
            if (e.getValue().size() < 2) continue;
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);

            TextView lbl = new TextView(this);
            lbl.setText(e.getKey());
            lbl.setTextSize(12);
            lbl.setPadding(0, 0, 8, 0);
            row.addView(lbl);

            // Display the shortened path, but remember the real one.
            final List<String> full = e.getValue();
            List<String> shown = new ArrayList<String>(full.size());
            for (String v : full) shown.add(shorten(full, v));

            Spinner sp = new Spinner(this);
            ArrayAdapter<String> ad = new ArrayAdapter<String>(
                    this, android.R.layout.simple_spinner_item, shown);
            ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
            sp.setAdapter(ad);
            final String key = e.getKey();
            sp.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
                @Override public void onItemSelected(android.widget.AdapterView<?> p,
                                                     View v, int pos, long id) {
                    variantChoice.put(key, pos);
                }
                @Override public void onNothingSelected(android.widget.AdapterView<?> p) { }
            });
            row.addView(sp, new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

            variantBox.addView(row);
        }
    }

    // ------------------------------------------------------------- apply/restore ----

    private String activeModId() {
        if (!activeFile.isFile()) return "";
        try { return readAll(activeFile).trim(); } catch (IOException e) { return ""; }
    }

    private void writeActive(String id) {
        try {
            stateDir.mkdirs();
            FileOutputStream fo = new FileOutputStream(activeFile);
            fo.write(id.getBytes("UTF-8"));
            fo.close();
        } catch (IOException e) {
            toast("Could not record active mod: " + e.getMessage());
        }
    }

    /**
     * Copy pristine bytes of everything data/ holds that the given mod will
     * overwrite. Done ONCE (the first time any mod is applied); afterwards the
     * backup is the authoritative "vanilla" copy.
     */
    private void ensureBackup(Mod m) throws IOException {
        if (backupDir.isDirectory()) {
            File[] any = backupDir.listFiles();
            if (any != null && any.length > 0) return;
        }
        for (Map.Entry<String, List<String>> e : m.files.entrySet()) {
            String pool = poolOf(e.getKey());
            File live = new File(new File(dataDir, pool), e.getKey());
            if (!live.isFile()) continue;
            File bak = new File(new File(backupDir, pool), e.getKey());
            bak.getParentFile().mkdirs();
            copyFile(live, bak);
        }
    }

    /** Put the saved pristine files back; leaves mod-only additions in place
     *  (harmless: they are extra files the vanilla script never opens). */
    private void restoreVanilla() throws IOException {
        File[] pools = backupDir.listFiles();
        if (pools == null) return;
        for (File pool : pools) {
            if (!pool.isDirectory()) continue;
            File[] files = pool.listFiles();
            if (files == null) continue;
            for (File bak : files) {
                copyFile(bak, new File(new File(dataDir, pool.getName()), bak.getName()));
            }
        }
    }

    private void doApplyAndPlay() {
        setButtonsEnabled(false);
        status.setText("Applying…");
        final Mod m = selected;
        final Map<String, Integer> picks = new LinkedHashMap<>(variantChoice);
        new Thread(new Runnable() {
            @Override public void run() {
                String msg;
                try {
                    if (m == null) {
                        restoreVanilla();
                        writeActive("");
                        msg = "Vanilla restored.";
                    } else {
                        String cur = activeModId();
                        if (cur.length() > 0 && !cur.equals(m.id)) restoreVanilla();
                        ensureBackup(m);
                        int n = 0;
                        for (Map.Entry<String, List<String>> e : m.files.entrySet()) {
                            List<String> variants = e.getValue();
                            int idx = picks.containsKey(e.getKey())
                                    ? picks.get(e.getKey()) : 0;
                            if (idx < 0 || idx >= variants.size()) idx = 0;
                            File src = new File(m.payload, variants.get(idx).replace('/', File.separatorChar));
                            String pool = poolOf(e.getKey());
                            File dst = new File(new File(dataDir, pool), e.getKey());
                            dst.getParentFile().mkdirs();
                            copyFile(src, dst);
                            n++;
                        }
                        writeActive(m.id);
                        msg = "Applied " + m.label + " (" + n + " files).";
                    }
                } catch (Exception e) {
                    msg = "Apply failed: " + e.getMessage();
                }
                final String out = msg;
                ui.post(new Runnable() {
                    @Override public void run() {
                        setButtonsEnabled(true);
                        status.setText(out);
                        launchGame();
                    }
                });
            }
        }).start();
    }

    private void doReset() {
        setButtonsEnabled(false);
        status.setText("Resetting…");
        new Thread(new Runnable() {
            @Override public void run() {
                String msg;
                try {
                    restoreVanilla();
                    writeActive("");
                    msg = "Back to vanilla.";
                } catch (Exception e) {
                    msg = "Reset failed: " + e.getMessage();
                }
                final String out = msg;
                ui.post(new Runnable() {
                    @Override public void run() {
                        setButtonsEnabled(true);
                        status.setText(out);
                    }
                });
            }
        }).start();
    }

    private void doDelete() {
        final Mod m = selected;
        if (m == null) return;
        new Thread(new Runnable() {
            @Override public void run() {
                String msg;
                try {
                    if (m.id.equals(activeModId())) {
                        restoreVanilla();
                        writeActive("");
                    }
                    deleteTree(m.dir);
                    msg = "Deleted " + m.label + ".";
                } catch (Exception e) {
                    msg = "Delete failed: " + e.getMessage();
                }
                final String out = msg;
                ui.post(new Runnable() {
                    @Override public void run() {
                        status.setText(out);
                        refreshMods();
                    }
                });
            }
        }).start();
    }

    // ------------------------------------------------------------------ import ----

    private void doImport() {
        Intent i = new Intent(Intent.ACTION_GET_CONTENT);
        i.setType("*/*");
        i.addCategory(Intent.CATEGORY_OPENABLE);
        try {
            startActivityForResult(Intent.createChooser(i, "Choose mod .zip"),
                    1001);
        } catch (Exception e) {
            toast("No file picker available: " + e.getMessage());
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != 1001 || res != RESULT_OK || data == null || data.getData() == null) return;
        final Uri src = data.getData();
        setButtonsEnabled(false);
        status.setText("Importing…");
        new Thread(new Runnable() {
            @Override public void run() {
                final String msg = importZip(src);
                ui.post(new Runnable() {
                    @Override public void run() {
                        setButtonsEnabled(true);
                        status.setText(msg);
                        refreshMods();
                    }
                });
            }
        }).start();
    }

    /**
     * Unpack a user-picked zip into mods/<name>/payload/.
     *
     * The packs published for this game are frequently BUNDLES - a zip holding
     * several inner zips, one per mod. When that is what we are given, each
     * inner zip becomes its own mod instead of being buried, which is what you
     * actually want to pick between later.
     */
    private String importZip(Uri uri) {
        File tmp = null;
        ZipFile zf = null;
        try {
            tmp = File.createTempFile("modimp", ".zip", filesDir);
            copyUriToFile(uri, tmp);

            // A bundle is a zip holding further zips, one per mod. Detect that
            // before unpacking so each inner pack becomes its own selectable mod.
            List<String> inner = new ArrayList<>();
            zf = new ZipFile(tmp);
            Enumeration<? extends ZipEntry> en = zf.entries();
            while (en.hasMoreElements()) {
                String n = en.nextElement().getName().toLowerCase();
                if (n.endsWith(".zip")) inner.add(n);
            }
            zf.close();
            zf = null;

            if (!inner.isEmpty()) {
                int made = 0;
                for (int k = 0; k < inner.size(); k++) {
                    File one = new File(filesDir, "inner_" + k + ".zip");
                    extractSingle(tmp, inner.get(k), one);
                    if (installPayload(one, sanitize(baseName(inner.get(k))),
                                       inner.get(k))) made++;
                    one.delete();
                }
                oneShotDelete(tmp);
                return "Imported " + made + " mod(s) from a bundle of " + inner.size() + ".";
            }

            String label = fileNameOf(uri);
            boolean ok = installPayload(tmp, sanitize(baseName(label)), label);
            oneShotDelete(tmp);
            return ok ? ("Installed \"" + sanitize(baseName(label)) + "\".")
                      : ("No game assets found in " + label + ".");
        } catch (Exception e) {
            if (zf != null) {
                try { zf.close(); } catch (IOException ignored) { }
            }
            if (tmp != null) oneShotDelete(tmp);
            return "Import failed: " + e.getMessage();
        }
    }

    /** Pull one named entry out of `src` into `dst`. */
    private void extractSingle(File src, String entryName, File dst) throws IOException {
        ZipFile zf = new ZipFile(src);
        ZipEntry found = null;
        Enumeration<? extends ZipEntry> en = zf.entries();
        while (en.hasMoreElements()) {
            ZipEntry z = en.nextElement();
            if (z.getName().equals(entryName)) { found = z; break; }
        }
        if (found == null) { zf.close(); throw new IOException("no entry " + entryName); }
        InputStream in = zf.getInputStream(found);
        OutputStream out = new FileOutputStream(dst);
        try {
            byte[] b = new byte[32768];
            int r;
            while ((r = in.read(b)) > 0) out.write(b, 0, r);
        } finally {
            try { in.close(); } catch (IOException ignored) { }
            out.close();
            zf.close();
        }
    }

    /** Unpack `zip` into mods/<id>/payload/, skipping junk and zip-bombs-ish paths. */
    private boolean installPayload(File zip, String id, String label) throws IOException {
        File dir = new File(modsDir, id);
        File payload = new File(dir, "payload");
        if (payload.exists()) deleteTree(payload);
        payload.mkdirs();

        int files = 0;
        ZipFile zf = new ZipFile(zip);
        Enumeration<? extends ZipEntry> en = zf.entries();
        while (en.hasMoreElements()) {
            ZipEntry z = en.nextElement();
            String n = z.getName().replace('\\', '/');
            if (z.isDirectory()) continue;
            if (n.startsWith("/") || n.contains("../")) continue;      // no escapes
            int slash = n.lastIndexOf('/');
            String base = (slash >= 0) ? n.substring(slash + 1) : n;
            if (base.length() == 0 || base.startsWith(".")) continue;   // skip dotfiles
            if (base.toLowerCase().endsWith(".rar")) continue;           // not supported

            File out = new File(payload, n);
            out.getParentFile().mkdirs();
            InputStream in = zf.getInputStream(z);
            OutputStream os = new FileOutputStream(out);
            try {
                byte[] b = new byte[32768];
                int r;
                while ((r = in.read(b)) > 0) os.write(b, 0, r);
            } finally {
                try { in.close(); } catch (IOException ignored) { }
                os.close();
            }
            files++;
        }
        zf.close();

        if (files == 0) { deleteTree(dir); return false; }

        // Remember a nicer display name when the pack shipped a readme.
        File nm = new File(dir, "name.txt");
        FileOutputStream fo = new FileOutputStream(nm);
        fo.write(label.replace('_', ' ').getBytes("UTF-8"));
        fo.close();
        return true;
    }

    // ------------------------------------------------------------------ launch ----

    private void launchGame() {
        Intent i = new Intent(this, org.libsdl.app.SDLActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(i);
        finish();
    }

    // ------------------------------------------------------------------ utils ----

    private void copyUriToFile(Uri uri, File dst) throws IOException {
        InputStream in = getContentResolver().openInputStream(uri);
        if (in == null) throw new IOException("cannot open " + uri);
        OutputStream out = new FileOutputStream(dst);
        try {
            byte[] b = new byte[32768];
            int r;
            while ((r = in.read(b)) > 0) out.write(b, 0, r);
        } finally {
            try { in.close(); } catch (IOException ignored) { }
            out.close();
        }
    }

    private static void copyFile(File src, File dst) throws IOException {
        dst.getParentFile().mkdirs();
        InputStream in = new FileInputStream(src);
        OutputStream out = new FileOutputStream(dst);
        try {
            byte[] b = new byte[32768];
            int r;
            while ((r = in.read(b)) > 0) out.write(b, 0, r);
        } finally {
            try { in.close(); } catch (IOException ignored) { }
            out.close();
        }
    }

    private static String readAll(File f) throws IOException {
        InputStream in = new FileInputStream(f);
        try {
            java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
            byte[] b = new byte[4096];
            int r;
            while ((r = in.read(b)) > 0) bo.write(b, 0, r);
            return bo.toString("UTF-8");
        } finally {
            in.close();
        }
    }

    private static String fileNameOf(Uri uri) {
        String s = uri.getLastPathSegment();
        if (s == null) return "mod.zip";
        int slash = Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\'));
        return slash >= 0 ? s.substring(slash + 1) : s;
    }

    private static String baseName(String fname) {
        int dot = fname.lastIndexOf('.');
        String b = dot > 0 ? fname.substring(0, dot) : fname;
        return b.length() > 0 ? b : "mod";
    }

    private static String sanitize(String s) {
        StringBuilder sb = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (Character.isLetterOrDigit(c) || c == '_' || c == '-' || c == ' ')
                sb.append(c);
            else sb.append('_');
        }
        String r = sb.toString().trim();
        return r.length() > 0 ? r : "mod";
    }

    private static int countFiles(File dir) {
        int[] n = new int[1];
        walk(dir, n);
        return n[0];
    }

    private static void walk(File d, int[] n) {
        File[] k = d.listFiles();
        if (k == null) return;
        for (File f : k) {
            if (f.isDirectory()) walk(f, n);
            else n[0]++;
        }
    }

    private static void deleteTree(File f) {
        if (f.isDirectory()) {
            File[] k = f.listFiles();
            if (k != null) for (File c : k) deleteTree(c);
        }
        f.delete();
    }

    private static void oneShotDelete(File f) {
        if (f != null) f.delete();
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }
}
