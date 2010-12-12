/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2010 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <dirent.h>
#include "converter.h"
#include "support.h"
#include "interface.h"
#include "gtkui.h"

ddb_encoder_preset_t *encoder_presets;
ddb_encoder_preset_t *current_encoder_preset;

ddb_dsp_preset_t *dsp_presets;
ddb_dsp_preset_t *current_dsp_preset;


ddb_encoder_preset_t *
ddb_encoder_preset_alloc (void) {
    ddb_encoder_preset_t *p = malloc (sizeof (ddb_encoder_preset_t));
    if (!p) {
        fprintf (stderr, "failed to alloc ddb_encoder_preset_t\n");
        return NULL;
    }
    memset (p, 0, sizeof (ddb_encoder_preset_t));
    return p;
}

void
ddb_encoder_preset_free (ddb_encoder_preset_t *p) {
    if (p) {
        if (p->title) {
            free (p->title);
        }
        if (p->fname) {
            free (p->fname);
        }
        if (p->encoder) {
            free (p->encoder);
        }
        free (p);
    }
}

ddb_encoder_preset_t *
ddb_encoder_preset_load (const char *fname) {
    int err = 1;
    FILE *fp = fopen (fname, "rt");
    if (!fp) {
        return NULL;
    }
    ddb_encoder_preset_t *p = ddb_encoder_preset_alloc ();

    char str[1024];

    if (1 != fscanf (fp, "title %1024[^\n]\n", str)) {
        goto error;
    }
    p->title = strdup (str);

    if (1 != fscanf (fp, "fname %1024[^\n]\n", str)) {
        goto error;
    }
    p->fname = strdup (str);

    if (1 != fscanf (fp, "encoder %1024[^\n]\n", str)) {
        goto error;
    }
    p->encoder = strdup (str);

    if (1 != fscanf (fp, "method %d\n", &p->method)) {
        goto error;
    }

    if (1 != fscanf (fp, "formats %X\n", &p->formats)) {
        goto error;
    }

    err = 0;
error:
    if (err) {
        ddb_encoder_preset_free (p);
        p = NULL;
    }
    if (fp) {
        fclose (fp);
    }
    return p;
}

// @return -1 on path/write error, -2 if file already exists
int
ddb_encoder_preset_save (ddb_encoder_preset_t *p, int overwrite) {
    const char *confdir = deadbeef->get_config_dir ();
    char path[1024];
    if (snprintf (path, sizeof (path), "%s/presets", confdir) < 0) {
        return -1;
    }
    mkdir (path, 0755);
    if (snprintf (path, sizeof (path), "%s/presets/encoders", confdir) < 0) {
        return -1;
    }
    mkdir (path, 0755);
    if (snprintf (path, sizeof (path), "%s/presets/encoders/%s.txt", confdir, p->title) < 0) {
        return -1;
    }

    if (!overwrite) {
        FILE *fp = fopen (path, "rb");
        if (fp) {
            fclose (fp);
            return -2; 
        }
    }

    FILE *fp = fopen (path, "w+b");
    if (!fp) {
        return -1;
    }

    fprintf (fp, "title %s\n", p->title);
    fprintf (fp, "fname %s\n", p->fname);
    fprintf (fp, "encoder %s\n", p->encoder);
    fprintf (fp, "method %d\n", p->method);
    fprintf (fp, "formats %08X\n", p->formats);

    fclose (fp);
    return 0;
}

ddb_dsp_preset_t *
ddb_dsp_preset_alloc (void) {
    ddb_dsp_preset_t *p = malloc (sizeof (ddb_dsp_preset_t));
    if (!p) {
        fprintf (stderr, "failed to alloc ddb_encoder_preset_t\n");
        return NULL;
    }
    memset (p, 0, sizeof (ddb_dsp_preset_t));
    return p;
}

void
ddb_dsp_preset_free (ddb_dsp_preset_t *p) {
    if (p) {
        if (p->title) {
            free (p->title);
        }
        while (p->chain) {
            DB_dsp_instance_t *next = p->chain->next;
            p->chain->plugin->close (p->chain);
            p->chain = next;
        }
        free (p);
    }
}

void
ddb_dsp_preset_copy (ddb_dsp_preset_t *to, ddb_dsp_preset_t *from) {
    to->title = strdup (from->title);
    DB_dsp_instance_t *tail = NULL;
    DB_dsp_instance_t *dsp = from->chain;
    while (dsp) {
        DB_dsp_instance_t *i = dsp->plugin->open ();
        if (dsp->plugin->num_params) {
            int n = dsp->plugin->num_params ();
            for (int j = 0; j < n; j++) {
                i->plugin->set_param (i, j, dsp->plugin->get_param (dsp, j));
            }
        }
        if (tail) {
            tail->next = i;
            tail = i;
        }
        else {
            to->chain = tail = i;
        }
        dsp = dsp->next;
    }
}

ddb_dsp_preset_t *
ddb_dsp_preset_load (const char *fname) {
    int err = 1;
    FILE *fp = fopen (fname, "rt");
    if (!fp) {
        return NULL;
    }
    ddb_dsp_preset_t *p = ddb_dsp_preset_alloc ();
    if (!p) {
        goto error;
    }

    // title
    char temp[100];
    if (1 != fscanf (fp, "title %100[^\n]\n", temp)) {
        goto error;
    }
    p->title = strdup (temp);
    DB_dsp_instance_t *tail = NULL;

    for (;;) {
        // plugin {
        int err = fscanf (fp, "%100s {\n", temp);
        if (err == EOF) {
            break;
        }
        else if (1 != err) {
            fprintf (stderr, "error plugin name\n");
            goto error;
        }

        DB_dsp_t *plug = (DB_dsp_t *)deadbeef->plug_get_for_id (temp);
        if (!plug) {
            fprintf (stderr, "ddb_dsp_preset_load: plugin %s not found. preset will not be loaded\n", temp);
            goto error;
        }
        DB_dsp_instance_t *inst = plug->open ();
        if (!inst) {
            fprintf (stderr, "ddb_dsp_preset_load: failed to open instance of plugin %s\n", temp);
            goto error;
        }

        if (tail) {
            tail->next = inst;
            tail = inst;
        }
        else {
            tail = p->chain = inst;
        }

        int n = 0;
        for (;;) {
            float value;
            if (!fgets (temp, sizeof (temp), fp)) {
                fprintf (stderr, "unexpected eof while reading plugin params\n");
                goto error;
            }
            if (!strcmp (temp, "}\n")) {
                break;
            }
            else if (1 != sscanf (temp, "\t%f\n", &value)) {
                fprintf (stderr, "error loading param %d\n", n);
                goto error;
            }
            if (plug->num_params) {
                plug->set_param (inst, n, value);
            }
            n++;
        }
    }

    err = 0;
error:
    if (err) {
        fprintf (stderr, "error loading %s\n", fname);
    }
    if (fp) {
        fclose (fp);
    }
    if (err && p) {
        ddb_dsp_preset_free (p);
        p = NULL;
    }
    return p;
}

int
ddb_dsp_preset_save (ddb_dsp_preset_t *p, int overwrite) {
    const char *confdir = deadbeef->get_config_dir ();
    char path[1024];
    if (snprintf (path, sizeof (path), "%s/presets", confdir) < 0) {
        return -1;
    }
    mkdir (path, 0755);
    if (snprintf (path, sizeof (path), "%s/presets/dsp", confdir) < 0) {
        return -1;
    }
    mkdir (path, 0755);
    if (snprintf (path, sizeof (path), "%s/presets/dsp/%s.txt", confdir, p->title) < 0) {
        return -1;
    }

    if (!overwrite) {
        FILE *fp = fopen (path, "rb");
        if (fp) {
            fclose (fp);
            return -2; 
        }
    }

    FILE *fp = fopen (path, "w+b");
    if (!fp) {
        return -1;
    }

    fprintf (fp, "title %s\n", p->title);

    DB_dsp_instance_t *inst = p->chain;
    while (inst) {
        fprintf (fp, "%s {\n", inst->plugin->plugin.id);
        if (inst->plugin->num_params) {
            int n = inst->plugin->num_params ();
            int i;
            for (i = 0; i < n; i++) {
                float v = inst->plugin->get_param (inst, i);
                fprintf (fp, "\t%f\n", v);
            }
        }
        fprintf (fp, "}\n");
        inst = inst->next;
    }

    fclose (fp);
    return 0;
}

static int dirent_alphasort (const struct dirent **a, const struct dirent **b) {
    return strcmp ((*a)->d_name, (*b)->d_name);
}

int
scandir_preset_filter (const struct dirent *ent) {
    char *ext = strrchr (ent->d_name, '.');
    if (ext && !strcasecmp (ext, ".txt")) {
        return 1;
    }
    return 0;
}

int
load_encoder_presets (void) {
    ddb_encoder_preset_t *tail = NULL;
    char path[1024];
    if (snprintf (path, sizeof (path), "%s/presets/encoders", deadbeef->get_config_dir ()) < 0) {
        return -1;
    }
    struct dirent **namelist = NULL;
    int n = scandir (path, &namelist, scandir_preset_filter, dirent_alphasort);
    int i;
    for (i = 0; i < n; i++) {
        char s[1024];
        if (snprintf (s, sizeof (s), "%s/%s", path, namelist[i]->d_name) > 0){
            ddb_encoder_preset_t *p = ddb_encoder_preset_load (s);
            if (p) {
                if (tail) {
                    tail->next = p;
                    tail = p;
                }
                else {
                    encoder_presets = tail = p;
                }
            }
        }
        free (namelist[i]);
    }
    free (namelist);
    return 0;
}

static GtkWidget *converter;

void
fill_presets (GtkListStore *mdl, ddb_preset_t *head) {
    ddb_preset_t *p = head;
    while (p) {
        GtkTreeIter iter;
        gtk_list_store_append (mdl, &iter);
        gtk_list_store_set (mdl, &iter, 0, p->title, -1);
        p = p->next;
    }
}

int
load_dsp_presets (void) {
    ddb_dsp_preset_t *tail = NULL;
    char path[1024];
    if (snprintf (path, sizeof (path), "%s/presets/dsp", deadbeef->get_config_dir ()) < 0) {
        return -1;
    }
    struct dirent **namelist = NULL;
    int n = scandir (path, &namelist, scandir_preset_filter, dirent_alphasort);
    int i;
    for (i = 0; i < n; i++) {
        char s[1024];
        if (snprintf (s, sizeof (s), "%s/%s", path, namelist[i]->d_name) > 0){
            ddb_dsp_preset_t *p = ddb_dsp_preset_load (s);
            if (p) {
                if (tail) {
                    tail->next = p;
                    tail = p;
                }
                else {
                    dsp_presets = tail = p;
                }
            }
        }
        free (namelist[i]);
    }
    free (namelist);

    // prepend empty preset
    ddb_dsp_preset_t *p = ddb_dsp_preset_alloc ();
    p->title = strdup ("Pass through");
    p->next = dsp_presets;
    dsp_presets = p;
    return 0;
}

void
converter_show (void) {
    if (!converter) {
        if (!encoder_presets) {
            load_encoder_presets ();
        }
        if (!dsp_presets) {
            load_dsp_presets ();
        }

        converter = create_converterdlg ();
        gtk_entry_set_text (GTK_ENTRY (lookup_widget (converter, "output_folder")), deadbeef->conf_get_str ("converter.output_folder", ""));

        GtkComboBox *combo;
        // fill encoder presets
        combo = GTK_COMBO_BOX (lookup_widget (converter, "encoder"));
        GtkListStore *mdl = GTK_LIST_STORE (gtk_combo_box_get_model (combo));
        fill_presets (mdl, (ddb_preset_t *)encoder_presets);
        gtk_combo_box_set_active (combo, deadbeef->conf_get_int ("converter.encoder_preset", 0));

        // fill dsp presets
        combo = GTK_COMBO_BOX (lookup_widget (converter, "dsp_preset"));
        mdl = GTK_LIST_STORE (gtk_combo_box_get_model (combo));
        fill_presets (mdl, (ddb_preset_t *)dsp_presets);
        gtk_combo_box_set_active (combo, deadbeef->conf_get_int ("converter.dsp_preset", 0));
        
        // fill channel maps
        combo = GTK_COMBO_BOX (lookup_widget (converter, "channelmap"));
        gtk_combo_box_set_active (combo, deadbeef->conf_get_int ("converter.channelmap_preset", 0));

        // select output format
        combo = GTK_COMBO_BOX (lookup_widget (converter, "output_format"));
        gtk_combo_box_set_active (combo, deadbeef->conf_get_int ("converter.output_format", 0));

    }
    gtk_widget_show (converter);
}

void
on_converter_encoder_changed           (GtkComboBox     *combobox,
                                        gpointer         user_data)
{
    GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "encoder"));
    int act = gtk_combo_box_get_active (combo);
    deadbeef->conf_set_int ("converter.encoder_preset", act);
}

void
on_converter_dsp_preset_changed        (GtkComboBox     *combobox,
                                        gpointer         user_data)
{
    GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "dsp_preset"));
    int act = gtk_combo_box_get_active (combo);
    deadbeef->conf_set_int ("converter.dsp_preset", act);
}

void
on_converter_output_browse_clicked     (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *dlg = gtk_file_chooser_dialog_new (_("Select folder..."), GTK_WINDOW (converter), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (converter));

    gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dlg), FALSE);
    // restore folder
    gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dlg), deadbeef->conf_get_str ("filechooser.lastdir", ""));
    int response = gtk_dialog_run (GTK_DIALOG (dlg));
    // store folder
    gchar *folder = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dlg));
    if (folder) {
        deadbeef->conf_set_str ("filechooser.lastdir", folder);
        g_free (folder);
        deadbeef->sendmessage (M_CONFIGCHANGED, 0, 0, 0);
    }
    if (response == GTK_RESPONSE_OK) {
        folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dlg));
        gtk_widget_destroy (dlg);
        if (folder) {
            GtkWidget *entry = lookup_widget (converter, "output_folder");
            gtk_entry_set_text (GTK_ENTRY (entry), folder);
            g_free (folder);
        }
    }
    else {
        gtk_widget_destroy (dlg);
    }
}


void
on_converter_cancel_clicked            (GtkButton       *button,
                                        gpointer         user_data)
{
    gtk_widget_destroy (converter);
    converter = NULL;
}

DB_decoder_t *
plug_get_decoder_for_id (const char *id) {
    DB_decoder_t **plugins = deadbeef->plug_get_decoder_list ();
    for (int c = 0; plugins[c]; c++) {
        if (!strcmp (id, plugins[c]->plugin.id)) {
            return plugins[c];
        }
    }
    return NULL;
}


void
on_converter_ok_clicked                (GtkButton       *button,
                                        gpointer         user_data)
{
    char *outfolder = strdup (gtk_entry_get_text (GTK_ENTRY (lookup_widget (converter, "output_folder"))));
    deadbeef->conf_set_str ("converter.output_folder", outfolder);
    deadbeef->conf_save ();

    GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "encoder"));
    int enc_preset = gtk_combo_box_get_active (combo);
    if (enc_preset < 0) {
        fprintf (stderr, "Encoder preset not selected\n");
        return;
    }

    ddb_encoder_preset_t *p = encoder_presets;
    while (p && enc_preset--) {
        p = p->next;
    }
    if (!p) {
        return;
    }
    combo = GTK_COMBO_BOX (lookup_widget (converter, "dsp_preset"));
    int dsp_idx = gtk_combo_box_get_active (combo);

    combo = GTK_COMBO_BOX (lookup_widget (converter, "output_format"));
    int selected_format = gtk_combo_box_get_active (combo);

    gtk_widget_destroy (converter);
    converter = NULL;

    deadbeef->pl_lock ();

    // copy list
    int nsel = deadbeef->pl_getselcount ();
    if (0 < nsel) {
        DB_playItem_t **items = malloc (sizeof (DB_playItem_t *) * nsel);
        if (items) {
            int n = 0;
            DB_playItem_t *it = deadbeef->pl_get_first (PL_MAIN);
            while (it) {
                if (deadbeef->pl_is_selected (it)) {
                    assert (n < nsel);
                    deadbeef->pl_item_ref (it);
                    items[n++] = it;
                }
                DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
                deadbeef->pl_item_unref (it);
                it = next;
            }
            deadbeef->pl_unlock ();

            ddb_dsp_preset_t *dsp_preset = NULL;
            if (dsp_idx > 0) {
                dsp_preset = dsp_presets;
                while (dsp_preset && dsp_idx--) {
                    dsp_preset = dsp_preset->next;
                }
            }

            for (n = 0; n < nsel; n++) {
                it = items[n];
                DB_decoder_t *dec = NULL;
                dec = plug_get_decoder_for_id (items[n]->decoder_id);
                if (dec) {
                    DB_fileinfo_t *fileinfo = dec->open (0);
                    if (fileinfo && dec->init (fileinfo, DB_PLAYITEM (it)) != 0) {
                        dec->free (fileinfo);
                        fileinfo = NULL;
                    }
                    if (fileinfo) {
                        char fname[1024];
                        int idx = deadbeef->pl_get_idx_of (it);
                        deadbeef->pl_format_title (it, idx, fname, sizeof (fname), -1, p->fname);
                        char out[1024];
                        snprintf (out, sizeof (out), "%s/%s", outfolder, fname);
                        char enc[1024];
                        snprintf (enc, sizeof (enc), p->encoder, out);
                        fprintf (stderr, "executing: %s\n", enc);
                        FILE *enc_pipe = NULL;
                        FILE *temp_file = NULL;

                        if (p->method == DDB_ENCODER_METHOD_FILE) {
                            const char *temp_file_name = "/tmp/deadbeef-converter.wav"; // FIXME
                            temp_file = fopen (temp_file_name, "w+b");
                            if (!temp_file) {
                                fprintf (stderr, "converter: failed to open temp file %s\n", temp_file_name);
                                if (fileinfo) {
                                    dec->free (fileinfo);
                                }
                                continue;
                            }
                        }
                        else {
                            enc_pipe = popen (enc, "w");
                            if (!enc_pipe) {
                                fprintf (stderr, "converter: failed to open encoder\n");
                                if (temp_file) {
                                    fclose (temp_file);
                                }
                                if (fileinfo) {
                                    dec->free (fileinfo);
                                }
                                continue;
                            }
                        }

                        if (!temp_file) {
                            temp_file = enc_pipe;
                        }

                        // write wave header
                        char wavehdr[] = {
                            0x52, 0x49, 0x46, 0x46, 0x24, 0x70, 0x0d, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6d, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x44, 0xac, 0x00, 0x00, 0x10, 0xb1, 0x02, 0x00, 0x04, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61
                        };
                        int header_written = 0;
                        uint32_t outsize = 0;
                        uint32_t outsr = fileinfo->fmt.samplerate;
                        uint16_t outch = fileinfo->fmt.channels;
                        uint16_t outbps = fileinfo->fmt.bps;
                        if (selected_format != 0) {
                            switch (selected_format) {
                            case 1 ... 4:
                                outbps = selected_format * 8;
                                break;
                            case 5:
                                outbps = 32;
                                break;
                            }
                        }

                        int samplesize = fileinfo->fmt.channels * fileinfo->fmt.bps / 8;
                        int bs = 10250 * samplesize;
                        char buffer[bs * 4];
                        int dspsize = bs / samplesize * sizeof (float) * fileinfo->fmt.channels;
                        char dspbuffer[dspsize * 4];
                        int eof = 0;
                        for (;;) {
                            if (eof) {
                                break;
                            }
                            int sz = dec->read (fileinfo, buffer, bs);

                            if (sz != bs) {
                                eof = 1;
                            }
                            float ratio = 1;
                            if (dsp_preset) {
                                ddb_waveformat_t fmt;
                                ddb_waveformat_t outfmt;
                                memcpy (&fmt, &fileinfo->fmt, sizeof (fmt));
                                memcpy (&outfmt, &fileinfo->fmt, sizeof (fmt));
                                fmt.bps = 32;
                                fmt.is_float = 1;
                                deadbeef->pcm_convert (&fileinfo->fmt, buffer, &fmt, dspbuffer, sz);

                                DB_dsp_instance_t *dsp = dsp_preset->chain;
                                int frames = sz / samplesize;
                                while (dsp) {
                                    frames = dsp->plugin->process (dsp, (float *)dspbuffer, frames, &fmt.samplerate, &fmt.channels);
                                    dsp = dsp->next;
                                }

                                outsr = fmt.samplerate;
                                outch = fmt.channels;

                                outfmt.bps = outbps;
                                outfmt.channels = outch;
                                outfmt.samplerate = outsr;

                                int n = deadbeef->pcm_convert (&fmt, dspbuffer, &outfmt, buffer, frames * sizeof (float) * fmt.channels);
                                sz = n;//frames * outch * outbps / 8;
                            }
                            else if (fileinfo->fmt.bps != outbps, 1) {
                                ddb_waveformat_t outfmt;
                                memcpy (&outfmt, &fileinfo->fmt, sizeof (outfmt));
                                outfmt.bps = outbps;
                                outfmt.channels = outch;
                                outfmt.samplerate = outsr;

                                int frames = sz / samplesize;
                                int n = deadbeef->pcm_convert (&fileinfo->fmt, buffer, &outfmt, dspbuffer, frames * samplesize);
                                memcpy (buffer, dspbuffer, n);
                                sz = n;//frames * outch * outbps / 8;
                            }
                            outsize += sz;

                            if (!header_written) {
                                uint32_t size = (it->endsample-it->startsample) * outch * outbps / 8;
                                if (!size) {
                                    size = deadbeef->pl_get_item_duration (it) * fileinfo->fmt.samplerate * outch * outbps / 8;

                                }

                                if (outsr != fileinfo->fmt.samplerate) {
                                    uint64_t temp = size;
                                    temp *= outsr;
                                    temp /= fileinfo->fmt.samplerate;
                                    size  = temp;
                                }

                                memcpy (&wavehdr[22], &outch, 2);
                                memcpy (&wavehdr[24], &outsr, 4);
                                uint16_t blockalign = outch * outbps / 8;
                                memcpy (&wavehdr[32], &blockalign, 2);
                                memcpy (&wavehdr[34], &outbps, 2);

                                fwrite (wavehdr, 1, sizeof (wavehdr), temp_file);
                                fwrite (&size, 1, sizeof (size), temp_file);
                                header_written = 1;
                            }

                            fwrite (buffer, 1, sz, temp_file);
                        }
                        if (temp_file && temp_file != enc_pipe) {
                            fclose (temp_file);
                        }

                        if (p->method == DDB_ENCODER_METHOD_FILE) {
                            enc_pipe = popen (enc, "w");
                        }

                        if (enc_pipe) {
                            pclose (enc_pipe);
                        }
                        dec->free (fileinfo);
                    }
                }
            }

            for (n = 0; n < nsel; n++) {
                deadbeef->pl_item_unref (items[n]);
            }
            free (items);
        }
        else {
            fprintf (stderr, "converter: error allocating memory to store %d tracks\n", nsel);
            deadbeef->pl_unlock ();
        }
    }


    free (outfolder);
}

gboolean
on_converterdlg_delete_event           (GtkWidget       *widget,
                                        GdkEvent        *event,
                                        gpointer         user_data)
{
    converter = NULL;
    return FALSE;
}

void
init_encoder_preset_from_dlg (GtkWidget *dlg, ddb_encoder_preset_t *p) {
    p->title = strdup (gtk_entry_get_text (GTK_ENTRY (lookup_widget (dlg, "title"))));
    p->fname = strdup (gtk_entry_get_text (GTK_ENTRY (lookup_widget (dlg, "fname"))));
    p->encoder = strdup (gtk_entry_get_text (GTK_ENTRY (lookup_widget (dlg, "encoder"))));
    int method_idx = gtk_combo_box_get_active (GTK_COMBO_BOX (lookup_widget (dlg, "method")));
    switch (method_idx) {
    case 0:
        p->method = DDB_ENCODER_METHOD_PIPE;
        break;
    case 1:
        p->method = DDB_ENCODER_METHOD_FILE;
        break;
    }

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_8bit")))) {
        p->formats |= DDB_ENCODER_FMT_8BIT;
    }
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_16bit")))) {
        p->formats |= DDB_ENCODER_FMT_16BIT;
    }
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_24bit")))) {
        p->formats |= DDB_ENCODER_FMT_24BIT;
    }
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_32bit")))) {
        p->formats |= DDB_ENCODER_FMT_32BIT;
    }
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_32bitfloat")))) {
        p->formats |= DDB_ENCODER_FMT_32BITFLOAT;
    }
}

int
edit_encoder_preset (char *title, GtkWidget *toplevel, int overwrite) {
    GtkWidget *dlg = create_convpreset_editor ();
    gtk_window_set_title (GTK_WINDOW (dlg), title);
    gtk_dialog_set_default_response (GTK_DIALOG (dlg), GTK_RESPONSE_OK);

    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (toplevel));

    ddb_encoder_preset_t *p = current_encoder_preset;

    if (p->title) {
        gtk_entry_set_text (GTK_ENTRY (lookup_widget (dlg, "title")), p->title);
    }
    if (p->fname) {
        gtk_entry_set_text (GTK_ENTRY (lookup_widget (dlg, "fname")), p->fname);
    }
    if (p->encoder) {
        gtk_entry_set_text (GTK_ENTRY (lookup_widget (dlg, "encoder")), p->encoder);
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (lookup_widget (dlg, "method")), p->method);
    if (p->formats & DDB_ENCODER_FMT_8BIT) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_8bit")), 1);
    }
    if (p->formats & DDB_ENCODER_FMT_16BIT) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_16bit")), 1);
    }
    if (p->formats & DDB_ENCODER_FMT_24BIT) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_24bit")), 1);
    }
    if (p->formats & DDB_ENCODER_FMT_32BIT) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_32bit")), 1);
    }
    if (p->formats & DDB_ENCODER_FMT_32BITFLOAT) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lookup_widget (dlg, "_32bitfloat")), 1);
    }

    ddb_encoder_preset_t *old = p;
    int r = GTK_RESPONSE_CANCEL;
    for (;;) {
        r = gtk_dialog_run (GTK_DIALOG (dlg));
        if (r == GTK_RESPONSE_OK) {
            ddb_encoder_preset_t *p = ddb_encoder_preset_alloc ();
            if (p) {
                init_encoder_preset_from_dlg (dlg, p);
                int err = ddb_encoder_preset_save (p, overwrite);
                if (!err) {
                    if (old->title && strcmp (p->title, old->title)) {
                        char path[1024];
                        if (snprintf (path, sizeof (path), "%s/presets/encoders/%s.txt", deadbeef->get_config_dir (), old->title) > 0) {
                            unlink (path);
                        }
                    }
                    free (old->title);
                    free (old->fname);
                    free (old->encoder);
                    old->title = p->title;
                    old->fname = p->fname;
                    old->encoder = p->encoder;
                    old->method = p->method;
                    old->formats = p->formats;
                    free (p);
                }
                else {
                    GtkWidget *warndlg = gtk_message_dialog_new (GTK_WINDOW (mainwin), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Failed to save encoder preset"));
                    gtk_window_set_transient_for (GTK_WINDOW (warndlg), GTK_WINDOW (dlg));
                    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (warndlg), err == -1 ? _("Check preset folder permissions, try to pick different title, or free up some disk space") : _("Preset with the same name already exists. Try to pick another title."));
                    gtk_window_set_title (GTK_WINDOW (warndlg), _("Error"));

                    int response = gtk_dialog_run (GTK_DIALOG (warndlg));
                    gtk_widget_destroy (warndlg);
                    continue;
                }
            }
        }
        break;
    }
    
    gtk_widget_destroy (dlg);
    return r;
}

void
refresh_encoder_lists (GtkComboBox *combo, GtkTreeView *list) {
    // presets list view
    GtkListStore *mdl = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (list)));

    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (path && col) {
        int *indices = gtk_tree_path_get_indices (path);
        int idx = *indices;
        g_free (indices);

        gtk_list_store_clear (mdl);
        fill_presets (mdl, (ddb_preset_t *)encoder_presets);
        path = gtk_tree_path_new_from_indices (idx, -1);
        gtk_tree_view_set_cursor (GTK_TREE_VIEW (list), path, col, FALSE);
        gtk_tree_path_free (path);
    }

    // presets combo box
    int act = gtk_combo_box_get_active (combo);
    mdl = GTK_LIST_STORE (gtk_combo_box_get_model (combo));
    gtk_list_store_clear (mdl);
    fill_presets (mdl, (ddb_preset_t *)encoder_presets);
    gtk_combo_box_set_active (combo, act);
}

void
on_encoder_preset_add                     (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));

    current_encoder_preset = ddb_encoder_preset_alloc ();
    if (GTK_RESPONSE_OK == edit_encoder_preset (_("Add new encoder"), toplevel, 0)) {
        // append
        ddb_encoder_preset_t *tail = encoder_presets;
        while (tail && tail->next) {
            tail = tail->next;
        }
        if (tail) {
            tail->next = current_encoder_preset;
        }
        else {
            encoder_presets = current_encoder_preset;
        }
        GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "encoder"));
        GtkWidget *list = lookup_widget (toplevel, "presets");
        refresh_encoder_lists (combo, GTK_TREE_VIEW (list));
    }

    current_encoder_preset = NULL;
}

void
on_encoder_preset_edit                     (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    GtkWidget *list = lookup_widget (toplevel, "presets");
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (!path || !col) {
        // nothing selected
        return;
    }
    int *indices = gtk_tree_path_get_indices (path);
    int idx = *indices;
    g_free (indices);

    ddb_encoder_preset_t *p = encoder_presets;
    while (idx--) {
        p = p->next;
    }
    current_encoder_preset = p;

    int r = edit_encoder_preset (_("Edit encoder"), toplevel, 1);
    if (r == GTK_RESPONSE_OK) {
        GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "encoder"));
        refresh_encoder_lists (combo, GTK_TREE_VIEW (list));
    }

    current_encoder_preset = NULL;
}

void
on_encoder_preset_remove                     (GtkButton       *button,
                                        gpointer         user_data)
{

    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    GtkWidget *list = lookup_widget (toplevel, "presets");
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (!path || !col) {
        // nothing selected
        return;
    }
    int *indices = gtk_tree_path_get_indices (path);
    int idx = *indices;
    g_free (indices);

    ddb_encoder_preset_t *p = encoder_presets;
    ddb_encoder_preset_t *prev = NULL;
    while (idx--) {
        prev = p;
        p = p->next;
    }

    if (!p) {
        return;
    }

    GtkWidget *dlg = gtk_message_dialog_new (GTK_WINDOW (mainwin), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, _("Remove preset"));
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (toplevel));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dlg), _("This action will delete the selected preset. Are you sure?"));
    gtk_window_set_title (GTK_WINDOW (dlg), _("Warning"));

    int response = gtk_dialog_run (GTK_DIALOG (dlg));
    gtk_widget_destroy (dlg);
    if (response == GTK_RESPONSE_YES) {
        char path[1024];
        if (snprintf (path, sizeof (path), "%s/presets/encoders/%s.txt", deadbeef->get_config_dir (), p->title) > 0) {
            unlink (path);
        }

        ddb_encoder_preset_t *next = p->next;
        if (prev) {
            prev->next = next;
        }
        else {
            encoder_presets = next;
        }
        ddb_encoder_preset_free (p);

        GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "encoder"));
        refresh_encoder_lists (combo, GTK_TREE_VIEW (list));
    }
}

void
on_edit_encoder_presets_clicked        (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *dlg = create_preset_list ();
    gtk_window_set_title (GTK_WINDOW (dlg), _("Encoders"));
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (converter));
    g_signal_connect ((gpointer)lookup_widget (dlg, "add"), "clicked", G_CALLBACK (on_encoder_preset_add), NULL);
    g_signal_connect ((gpointer)lookup_widget (dlg, "remove"), "clicked", G_CALLBACK (on_encoder_preset_remove), NULL);
    g_signal_connect ((gpointer)lookup_widget (dlg, "edit"), "clicked", G_CALLBACK (on_encoder_preset_edit), NULL);

    GtkWidget *list = lookup_widget (dlg, "presets");
    GtkCellRenderer *title_cell = gtk_cell_renderer_text_new ();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes (_("Title"), title_cell, "text", 0, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), GTK_TREE_VIEW_COLUMN (col));
    GtkListStore *mdl = gtk_list_store_new (1, G_TYPE_STRING);
    gtk_tree_view_set_model (GTK_TREE_VIEW (list), GTK_TREE_MODEL (mdl));
    fill_presets (mdl, (ddb_preset_t *)encoder_presets);
    int curr = deadbeef->conf_get_int ("converter.encoder_preset", 0);
    GtkTreePath *path = gtk_tree_path_new_from_indices (curr, -1);
    gtk_tree_view_set_cursor (GTK_TREE_VIEW (list), path, col, FALSE);
    gtk_tree_path_free (path);
    gtk_dialog_run (GTK_DIALOG (dlg));
    gtk_widget_destroy (dlg);
}

///// dsp preset gui

void
fill_dsp_plugin_list (GtkListStore *mdl) {
    struct DB_dsp_s **dsp = deadbeef->plug_get_dsp_list ();
    int i;
    for (i = 0; dsp[i]; i++) {
        GtkTreeIter iter;
        gtk_list_store_append (mdl, &iter);
        gtk_list_store_set (mdl, &iter, 0, dsp[i]->plugin.name, -1);
    }
}

void
fill_dsp_preset_chain (GtkListStore *mdl) {
    DB_dsp_instance_t *dsp = current_dsp_preset->chain;
    while (dsp) {
        GtkTreeIter iter;
        gtk_list_store_append (mdl, &iter);
        gtk_list_store_set (mdl, &iter, 0, dsp->plugin->plugin.name, -1);
        dsp = dsp->next;
    }
}

void
on_dsp_preset_add_plugin_clicked       (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *dlg = create_select_dsp_plugin ();
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (toplevel));
    gtk_window_set_title (GTK_WINDOW (dlg), _("Add plugin to DSP chain"));

    GtkComboBox *combo;
    // fill encoder presets
    combo = GTK_COMBO_BOX (lookup_widget (dlg, "plugin"));
    GtkListStore *mdl = GTK_LIST_STORE (gtk_combo_box_get_model (combo));
    fill_dsp_plugin_list (mdl);
    gtk_combo_box_set_active (combo, deadbeef->conf_get_int ("converter.last_selected_dsp", 0));

    int r = gtk_dialog_run (GTK_DIALOG (dlg));
    if (r == GTK_RESPONSE_OK) {
        // create new instance of the selected plugin
        int idx = gtk_combo_box_get_active (combo);
        struct DB_dsp_s **dsp = deadbeef->plug_get_dsp_list ();
        int i;
        DB_dsp_instance_t *inst = NULL;
        for (i = 0; dsp[i]; i++) {
            if (i == idx) {
                inst = dsp[i]->open ();
                break;
            }
        }
        if (inst) {
            // append to DSP chain
            DB_dsp_instance_t *tail = current_dsp_preset->chain;
            while (tail && tail->next) {
                tail = tail->next;
            }
            if (tail) {
                tail->next = inst;
            }
            else {
                current_dsp_preset->chain = inst;
            }

            // reinit list of instances
            GtkWidget *list = lookup_widget (toplevel, "plugins");
            GtkListStore *mdl = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW(list)));
            gtk_list_store_clear (mdl);
            fill_dsp_preset_chain (mdl);
        }
        else {
            fprintf (stderr, "converter: failed to add DSP plugin to chain\n");
        }
    }
    gtk_widget_destroy (dlg);
}


void
on_dsp_preset_remove_plugin_clicked    (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    GtkWidget *list = lookup_widget (toplevel, "plugins");
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (!path || !col) {
        // nothing selected
        return;
    }
    int *indices = gtk_tree_path_get_indices (path);
    int idx = *indices;
    g_free (indices);
    if (idx == -1) {
        return;
    }

    DB_dsp_instance_t *p = current_dsp_preset->chain;
    DB_dsp_instance_t *prev = NULL;
    int i = idx;
    while (p && i--) {
        prev = p;
        p = p->next;
    }
    if (p) {
        if (prev) {
            prev->next = p->next;
        }
        else {
            current_dsp_preset->chain = p->next;
        }
        p->plugin->close (p);
        GtkListStore *mdl = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW(list)));
        gtk_list_store_clear (mdl);
        fill_dsp_preset_chain (mdl);
        path = gtk_tree_path_new_from_indices (idx, -1);
        gtk_tree_view_set_cursor (GTK_TREE_VIEW (list), path, col, FALSE);
        gtk_tree_path_free (path);
    }
}

void
on_dsp_preset_plugin_configure_clicked (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    GtkWidget *list = lookup_widget (toplevel, "plugins");
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (!path || !col) {
        // nothing selected
        return;
    }
    int *indices = gtk_tree_path_get_indices (path);
    int idx = *indices;
    g_free (indices);
    if (idx == -1) {
        return;
    }

    DB_dsp_instance_t *p = current_dsp_preset->chain;
    DB_dsp_instance_t *prev = NULL;
    int i = idx;
    while (p && i--) {
        prev = p;
        p = p->next;
    }
    if (!p || !p->plugin->num_params || !p->plugin->num_params ()) {
        return;
    }

    GtkWidget *dlg = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (dlg), _("Configure DSP plugin"));
    gtk_window_set_modal (GTK_WINDOW (dlg), TRUE);
    gtk_window_set_type_hint (GTK_WINDOW (dlg), GDK_WINDOW_TYPE_HINT_DIALOG);

    GtkWidget *vbox = gtk_vbox_new (FALSE, 8);
    gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
    gtk_widget_show (vbox);

    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), vbox, TRUE, TRUE, 0);

    GtkWidget *w;

    int n = p->plugin->num_params ();
    GtkEntry *entries[n];
    for (i = 0; i < n; i++) {
        GtkWidget *hbox = gtk_hbox_new (FALSE, 8);

        gtk_widget_show (hbox);
        gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
        const char *title = p->plugin->get_param_name (i);
        w = gtk_label_new (_(title));
        gtk_widget_show (w);
        gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
        entries[i] = GTK_ENTRY (gtk_entry_new ());
        char s[100];
        snprintf (s, sizeof (s), "%f", p->plugin->get_param (p, i));
        gtk_entry_set_text (entries[i], s);
        gtk_widget_show (GTK_WIDGET (entries[i]));
        gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (entries[i]), TRUE, TRUE, 0);
    }


    w = gtk_button_new_from_stock ("gtk-cancel");
    gtk_widget_show (w);
    gtk_dialog_add_action_widget (GTK_DIALOG (dlg), w, GTK_RESPONSE_CANCEL);
    GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT);

    w = gtk_button_new_from_stock ("gtk-ok");
    gtk_widget_show (w);
    gtk_dialog_add_action_widget (GTK_DIALOG (dlg), w, GTK_RESPONSE_OK);
    GTK_WIDGET_SET_FLAGS (w, GTK_CAN_DEFAULT);

    int r = gtk_dialog_run (GTK_DIALOG (dlg));
    if (r == GTK_RESPONSE_OK) {
        for (i = 0; i < n; i++) {
            const char *s = gtk_entry_get_text (entries[i]);
            p->plugin->set_param (p, i, atof (s));
        }
    }
    gtk_widget_destroy (dlg);
}

int
edit_dsp_preset (const char *title, GtkWidget *toplevel, int overwrite) {
    int r = GTK_RESPONSE_CANCEL;

    GtkWidget *dlg = create_dsppreset_editor ();
    gtk_dialog_set_default_response (GTK_DIALOG (dlg), GTK_RESPONSE_OK);
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (toplevel));
    gtk_window_set_title (GTK_WINDOW (dlg), title);


    // title
    if (current_dsp_preset->title) {
        gtk_entry_set_text (GTK_ENTRY (lookup_widget (dlg, "title")), current_dsp_preset->title);
    }

    {
        // left list
        GtkWidget *list = lookup_widget (dlg, "plugins");
        GtkCellRenderer *title_cell = gtk_cell_renderer_text_new ();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes (_("Plugin"), title_cell, "text", 0, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (list), GTK_TREE_VIEW_COLUMN (col));
        GtkListStore *mdl = gtk_list_store_new (1, G_TYPE_STRING);
        gtk_tree_view_set_model (GTK_TREE_VIEW (list), GTK_TREE_MODEL (mdl));

        fill_dsp_preset_chain (mdl);
    }

    for (;;) {
        r = gtk_dialog_run (GTK_DIALOG (dlg));

        if (r == GTK_RESPONSE_OK) {
            if (current_dsp_preset->title) {
                free (current_dsp_preset->title);
            }
            current_dsp_preset->title = strdup (gtk_entry_get_text (GTK_ENTRY (lookup_widget (dlg, "title"))));
            int err = ddb_dsp_preset_save (current_dsp_preset, overwrite);
            if (err < 0) {
                GtkWidget *warndlg = gtk_message_dialog_new (GTK_WINDOW (mainwin), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Failed to save DSP preset"));
                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (warndlg), err == -1 ? _("Check preset folder permissions, try to pick different title, or free up some disk space") : _("Preset with the same name already exists. Try to pick another title."));
                gtk_window_set_title (GTK_WINDOW (warndlg), _("Error"));

                gtk_window_set_transient_for (GTK_WINDOW (warndlg), GTK_WINDOW (dlg));
                int response = gtk_dialog_run (GTK_DIALOG (warndlg));
                gtk_widget_destroy (warndlg);
                continue;
            }

        }

        break;
    }

    gtk_widget_destroy (dlg);
    return r;
}

void
refresh_dsp_lists (GtkComboBox *combo, GtkTreeView *list) {
    // presets list view
    GtkListStore *mdl = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (list)));

    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (path && col) {
        int *indices = gtk_tree_path_get_indices (path);
        int idx = *indices;
        g_free (indices);

        gtk_list_store_clear (mdl);
        fill_presets (mdl, (ddb_preset_t *)dsp_presets);
        path = gtk_tree_path_new_from_indices (idx, -1);
        gtk_tree_view_set_cursor (GTK_TREE_VIEW (list), path, col, FALSE);
        gtk_tree_path_free (path);
    }

    // presets combo box
    int act = gtk_combo_box_get_active (combo);
    mdl = GTK_LIST_STORE (gtk_combo_box_get_model (combo));
    gtk_list_store_clear (mdl);
    fill_presets (mdl, (ddb_preset_t *)dsp_presets);
    gtk_combo_box_set_active (combo, act);
}


void
on_dsp_preset_add                     (GtkButton       *button,
                                        gpointer         user_data)
{

    current_dsp_preset = ddb_dsp_preset_alloc ();
    
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));

    if (GTK_RESPONSE_OK == edit_dsp_preset (_("New DSP Preset"), toplevel, 0)) {
        // append to list of presets
        ddb_dsp_preset_t *tail = dsp_presets;
        while (tail && tail->next) {
            tail = tail->next;
        }
        if (tail) {
            tail->next = current_dsp_preset;
        }
        else {
            dsp_presets = current_dsp_preset;
        }

        GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "dsp_preset"));
        GtkWidget *list = lookup_widget (toplevel, "presets");
        refresh_dsp_lists (combo, GTK_TREE_VIEW (list));
    }
    else {
        ddb_dsp_preset_free (current_dsp_preset);
    }

    current_dsp_preset = NULL;
}

void
on_dsp_preset_remove                     (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));
    GtkWidget *list = lookup_widget (toplevel, "presets");
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (!path || !col) {
        // nothing selected
        return;
    }
    int *indices = gtk_tree_path_get_indices (path);
    int idx = *indices;
    g_free (indices);

    if (idx == 0) {
        return;
    }

    ddb_dsp_preset_t *p = dsp_presets;
    ddb_dsp_preset_t *prev = NULL;
    while (idx--) {
        prev = p;
        p = p->next;
    }

    if (!p) {
        return;
    }

    GtkWidget *dlg = gtk_message_dialog_new (GTK_WINDOW (mainwin), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, _("Remove preset"));
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (toplevel));
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dlg), _("This action will delete the selected preset. Are you sure?"));
    gtk_window_set_title (GTK_WINDOW (dlg), _("Warning"));

    int response = gtk_dialog_run (GTK_DIALOG (dlg));
    gtk_widget_destroy (dlg);
    if (response == GTK_RESPONSE_YES) {
        char path[1024];
        if (snprintf (path, sizeof (path), "%s/presets/dsp/%s.txt", deadbeef->get_config_dir (), p->title) > 0) {
            unlink (path);
        }

        ddb_dsp_preset_t *next = p->next;
        if (prev) {
            prev->next = next;
        }
        else {
            dsp_presets = next;
        }
        ddb_dsp_preset_free (p);

        GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "dsp_preset"));
        refresh_dsp_lists (combo, GTK_TREE_VIEW (list));
    }
}

void
on_dsp_preset_edit                     (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (button));

    GtkWidget *list = lookup_widget (toplevel, "presets");
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (list), &path, &col);
    if (!path || !col) {
        // nothing selected
        return;
    }
    int *indices = gtk_tree_path_get_indices (path);
    int idx = *indices;
    g_free (indices);
    if (idx == -1) {
        return;
    }
    if (idx == 0) {
        return;
    }

    ddb_dsp_preset_t *prev = NULL;
    ddb_dsp_preset_t *p = dsp_presets;
    while (p && idx > 0) {
        prev = p;
        p = p->next;
        idx--;
    }
    if (!p) {
        return;
    }

    current_dsp_preset = ddb_dsp_preset_alloc ();
    ddb_dsp_preset_copy (current_dsp_preset, p);

    int r = edit_dsp_preset (_("Edit DSP Preset"), toplevel, 1);
    if (r == GTK_RESPONSE_OK) {
        // replace preset
        if (prev) {
            prev->next = current_dsp_preset;
        }
        else {
            dsp_presets = current_dsp_preset;
        }
        current_dsp_preset->next = p->next;
        ddb_dsp_preset_free (p);
        GtkComboBox *combo = GTK_COMBO_BOX (lookup_widget (converter, "dsp_preset"));
        refresh_dsp_lists (combo, GTK_TREE_VIEW (list));
    }
    else {
        ddb_dsp_preset_free (current_dsp_preset);
    }

    current_dsp_preset = NULL;
}

void
on_edit_dsp_presets_clicked            (GtkButton       *button,
                                        gpointer         user_data)
{
    GtkWidget *dlg = create_preset_list ();
    gtk_window_set_title (GTK_WINDOW (dlg), _("DSP Presets"));
    gtk_window_set_transient_for (GTK_WINDOW (dlg), GTK_WINDOW (converter));
    g_signal_connect ((gpointer)lookup_widget (dlg, "add"), "clicked", G_CALLBACK (on_dsp_preset_add), NULL);
    g_signal_connect ((gpointer)lookup_widget (dlg, "remove"), "clicked", G_CALLBACK (on_dsp_preset_remove), NULL);
    g_signal_connect ((gpointer)lookup_widget (dlg, "edit"), "clicked", G_CALLBACK (on_dsp_preset_edit), NULL);

    GtkWidget *list = lookup_widget (dlg, "presets");
    GtkCellRenderer *title_cell = gtk_cell_renderer_text_new ();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes (_("Title"), title_cell, "text", 0, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (list), GTK_TREE_VIEW_COLUMN (col));
    GtkListStore *mdl = gtk_list_store_new (1, G_TYPE_STRING);
    gtk_tree_view_set_model (GTK_TREE_VIEW (list), GTK_TREE_MODEL (mdl));
    fill_presets (mdl, (ddb_preset_t *)dsp_presets);
    int curr = deadbeef->conf_get_int ("converter.dsp_preset", 0);
    GtkTreePath *path = gtk_tree_path_new_from_indices (curr, -1);
    gtk_tree_view_set_cursor (GTK_TREE_VIEW (list), path, col, FALSE);
    gtk_tree_path_free (path);
    gtk_dialog_run (GTK_DIALOG (dlg));
    gtk_widget_destroy (dlg);
}


void
on_edit_channel_maps_clicked           (GtkButton       *button,
                                        gpointer         user_data)
{
}
