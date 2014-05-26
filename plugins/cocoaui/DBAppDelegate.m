/*
 DeaDBeeF Cocoa GUI
 Copyright (C) 2012 Carlos Nunes <carloslnunes@gmail.com>
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#import "DBAppDelegate.h"

#include "deadbeef.h"
#include "cocoaui_api.h"

@implementation DBAppDelegate

@synthesize mainWindow;
@synthesize mainPlaylist;
@synthesize fileImportPanel;


- (void)applicationWillTerminate:(NSNotification *)aNotification {

    // save config
    deadbeef->pl_save_all ();
    deadbeef->conf_save ();
}

- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename {
	
	[DBAppDelegate clearPlayList];
	BOOL inserted = [DBAppDelegate addPathsToPlaylistAt:[NSArray arrayWithObject:filename] row:-1 progressPanel: fileImportPanel ];
	if (inserted) {
		DBPlayListController * controller = (DBPlayListController *) [mainPlaylist delegate];
		[controller playSelectedItem: nil];
	}
		
	return inserted;
}

- (void)application:(NSApplication *)sender openFiles:(NSArray *)filenames {
	
	printf("Opening: %s\n", [[filenames objectAtIndex:0] UTF8String]);
	
	[DBAppDelegate clearPlayList];
	BOOL inserted = [DBAppDelegate addPathsToPlaylistAt:filenames row:-1 progressPanel: fileImportPanel ];
	if (inserted) {
		DBPlayListController * controller = (DBPlayListController *) [mainPlaylist delegate];
		[controller playSelectedItem: nil];
	}
		
	return;
}


- (NSMenu *) applicationDockMenu:(NSApplication *)sender {
	
	NSMenu * menu = [[NSMenu alloc] init];
	
	NSString * playToggleString = NULL;
	if([self isPlaying]) {
		playToggleString = NSLocalizedString(@"Pause", "Dock item");
		[menu addItemWithTitle:NSLocalizedString(@"Now playing", "Dock item") action:nil keyEquivalent:@""];
		NSString * trackName = [self playingTrackName];		
		[menu addItemWithTitle:[NSString stringWithFormat:@"%@%@", @"   ", trackName] action:nil keyEquivalent:@""];
		[menu addItem:[NSMenuItem separatorItem]];
	} else {
		playToggleString = NSLocalizedString(@"Play", "Dock item");
	}
	
	[menu addItemWithTitle: playToggleString action:@selector(togglePlay:) keyEquivalent:@""];
	[menu addItemWithTitle: NSLocalizedString(@"Next", "Dock item") action:@selector(nextAction:) keyEquivalent:@""];
	[menu addItemWithTitle: NSLocalizedString(@"Previous", "Dock item") action:@selector(previousAction:) keyEquivalent:@""];
	
	return [menu autorelease];
}


- (BOOL)applicationShouldHandleReopen:(NSApplication*)theApplication
					hasVisibleWindows:(BOOL)flag{
	[mainWindow orderFront:nil];
	[mainWindow makeKeyWindow];
	return TRUE;
}


- (IBAction) openPreferences : (id) sender {

	if (preferencesWindowController == nil) {
    
		NSViewController * viewControllerSound = [[DBPreferencesViewControllerSound alloc] init];
		NSViewController * viewControllerPlayback = [[DBPreferencesViewControllerPlayback alloc] init];	
		NSViewController * viewControllerNetwork = [[DBPreferencesViewControllerNetwork alloc] init];	
		NSViewController * viewControllerPlugins = [[DBPreferencesViewControllerPlugins alloc] init];	

		NSArray * controllers = [[NSArray alloc] initWithObjects:
								 viewControllerSound,
								 viewControllerPlayback,
								 viewControllerNetwork,
								 viewControllerPlugins,
								nil];
								 
		[viewControllerSound release];
		[viewControllerPlayback release];
		[viewControllerNetwork release];
		[viewControllerPlugins release];
		
		NSString * title = NSLocalizedString(@"Preferences", @"Common title for preferences window");
		preferencesWindowController = [[MASPreferencesWindowController alloc] initWithViewControllers:controllers title:title];
		
		[controllers release];
        
    }
	
	[preferencesWindowController showWindow : nil];

}


/* = deadbeef core functionality
 */


// callback function for the file/directory import operations
int ui_add_file_info_cb (DB_playItem_t *it, void *data) {
	
	DBFileImportPanel * panel = NULL;
	if (data != NULL)
		panel = (DBFileImportPanel *) data; 
	
	deadbeef->pl_lock ();
	
	if (panel != NULL) {
		if (![panel isVisible] || [panel abortPressed]) {
			deadbeef->pl_unlock ();
			return -1;
		}
	}
	
    const char *fname = deadbeef->pl_find_meta (it, ":URI");
	if ( panel == NULL) {
		printf("%s\n", fname);
	}
	else {
		dispatch_async(dispatch_get_main_queue(), ^{
			NSString *file = [NSString stringWithUTF8String: fname];
			[panel setCurrentFile:file];
		});
	}
    deadbeef->pl_unlock ();
    return 0;
	
}


- (BOOL) isPlaying {
		
	if ([DBAppDelegate outputState] == OUTPUT_STATE_PLAYING) 
		return YES;
	
	return NO;
}

- (NSString*) playingTrackName {
	
	deadbeef->pl_lock();
	
	const char * meta = NULL;
	DB_playItem_t * it = deadbeef->streamer_get_playing_track();
	
	
	meta = deadbeef->pl_find_meta_raw(it, "title");
	if (meta == NULL) {
		const char *f = deadbeef->pl_find_meta_raw (it, ":URI");
		meta = strrchr (f, '/');
		if (meta) {
			meta++;
		}
		else {
			meta = f;
		}
	}
	
	deadbeef->pl_unlock();
	deadbeef->pl_item_unref(it);
	
	return [NSString stringWithUTF8String: meta];
}

- (IBAction) stopAction : sender {
	
    deadbeef->sendmessage (DB_EV_STOP, 0, 0, 0);		
}


- (IBAction) previousAction : sender {
	
    deadbeef->sendmessage (DB_EV_PREV, 0, 0, 0);    
}

- (IBAction) nextAction : sender {
	
    deadbeef->sendmessage (DB_EV_NEXT, 0, 0, 0);
}


- (IBAction) togglePlay: sender 
{
	
	if ([DBAppDelegate outputState] == OUTPUT_STATE_STOPPED) 
	{
		deadbeef->sendmessage(DB_EV_PLAY_CURRENT, 0, 0, 0);
		return;
	}	
	
	deadbeef->sendmessage (DB_EV_TOGGLE_PAUSE, 0, 0, 0);
	
}


/*
 accepts both a NSURL list as well as a NSSring list
 */
+ (BOOL) addPathsToPlaylistAt : (NSArray *) list row:(NSInteger)rowIndex progressPanel : panel {
	
    ddb_playlist_t * plt = deadbeef->plt_get_curr ();
    if ( deadbeef->pl_add_files_begin (plt) < 0) {
        deadbeef->plt_unref (plt);
        return NO;
    }
	
	// the add files code is executed in another thread
	dispatch_async(dispatch_get_global_queue(0, 0),
				   ^ {					   
					   
					   DB_playItem_t * after = NULL;
					   DB_playItem_t * inserted = NULL;
					   int abort = 0;
					   NSString * file;
					   const char * path;
					   BOOL isDir;
					   
					   // if the provided row index is less than 0, the files will be added at the end of the current playlist
					   if (rowIndex < 0)
						   after = deadbeef->pl_get_last (PL_MAIN);
					   else
						   after = deadbeef->pl_get_for_idx(rowIndex - 1);
					   
					   [panel orderFront:self];
					   
					   for (int i=0; i<[list count]; ++i) {
						   // check for arg type
						   if([[list objectAtIndex:i] isKindOfClass:[NSURL class]]) { 
							   file = [[list objectAtIndex:i] path];
						   } else {
							   file = [list objectAtIndex:i];
						   }
						   
						   path = [file cStringUsingEncoding:NSUTF8StringEncoding];
						   
						   if([[NSFileManager defaultManager] fileExistsAtPath:file isDirectory:&isDir] && isDir){
							   inserted = deadbeef->plt_insert_dir (plt, after, path, &abort, ui_add_file_info_cb, panel);
						   } else {
							   inserted = deadbeef->plt_insert_file (plt, after, path, &abort, ui_add_file_info_cb, panel);
							   if (inserted)
								   [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL:[NSURL fileURLWithPath:file]];
						   }
						   
						   if (inserted) {
							   if (after) {
								   deadbeef->pl_item_unref (after);
							   }
							   after = inserted;
							   deadbeef->pl_item_ref (after);
						   }
						   // if aborted exit loop
						   if (abort)
							   break;
					   }
					   
					   if (after)
						   deadbeef->pl_item_unref (after);
					   
					   deadbeef->pl_add_files_end ();
					   deadbeef->plt_unref (plt);
					   deadbeef->pl_save_all ();
					   deadbeef->conf_save ();
					   
					   dispatch_async(dispatch_get_main_queue(), ^{ [panel close]; });
					   
				   });
	
	return YES;
}


+ (NSString *) totalPlaytimeAndSongCount {
	
	if (deadbeef->pl_getcount(PL_MAIN) == 0)
		return [NSString stringWithFormat:@"No songs"];
	
	float pl_totaltime = deadbeef->plt_get_totaltime(PL_MAIN);
	
    int daystotal = (int)pl_totaltime / (3600*24);
    int hourtotal = ((int)pl_totaltime / 3600) % 24;
    int mintotal = ((int)pl_totaltime/60) % 60;
    int sectotal = ((int)pl_totaltime) % 60;
	
	char totaltime_str[512] = "";
	if (daystotal > 1) {
        snprintf (totaltime_str, sizeof (totaltime_str), ", %d days", daystotal);		
	} else if (daystotal == 1) {
        snprintf (totaltime_str, sizeof (totaltime_str), ", 1 day");			
	}
	
	if(hourtotal) {
        snprintf (totaltime_str, sizeof (totaltime_str), "%s, %d hours", totaltime_str, hourtotal);				
	}
	
	if(mintotal) {
        snprintf (totaltime_str, sizeof (totaltime_str), "%s, %d minutes", totaltime_str, mintotal);				
	}
	
	if(sectotal) {
        snprintf (totaltime_str, sizeof (totaltime_str), "%s, %d seconds", totaltime_str, sectotal);				
	}	
	
	return [NSString stringWithFormat:@"%d song(s)%s", deadbeef->pl_getcount(PL_MAIN), totaltime_str];
}

/*
 - (void) updateSongInfo {
 
 DB_output_t *output = plug_get_output ();
 
 char sbtext_new[512] = "-";
 float songpos = last_songpos;
 
 float pl_totaltime = pl_get_totaltime ();
 int daystotal = (int)pl_totaltime / (3600*24);
 int hourtotal = ((int)pl_totaltime / 3600) % 24;
 int mintotal = ((int)pl_totaltime/60) % 60;
 int sectotal = ((int)pl_totaltime) % 60;
 
 char totaltime_str[512] = "";
 if (daystotal == 0) {
 snprintf (totaltime_str, sizeof (totaltime_str), "%d:%02d:%02d", hourtotal, mintotal, sectotal);
 }
 else if (daystotal == 1) {
 snprintf (totaltime_str, sizeof (totaltime_str), _("1 day %d:%02d:%02d"), hourtotal, mintotal, sectotal);
 }
 else {
 snprintf (totaltime_str, sizeof (totaltime_str), _("%d days %d:%02d:%02d"), daystotal, hourtotal, mintotal, sectotal);
 }
 
 DB_playItem_t *track = streamer_get_playing_track ();
 DB_fileinfo_t *c = streamer_get_current_fileinfo (); // FIXME: might crash streamer
 
 float duration = track ? pl_get_item_duration (track) : -1;
 
 if (!output || (output->state () == OUTPUT_STATE_STOPPED || !track || !c)) {
 snprintf (sbtext_new, sizeof (sbtext_new), _("Stopped | %d tracks | %s total playtime"), pl_getcount (PL_MAIN), totaltime_str);
 songpos = 0;
 }
 else {
 float playpos = streamer_get_playpos ();
 int minpos = playpos / 60;
 int secpos = playpos - minpos * 60;
 int mindur = duration / 60;
 int secdur = duration - mindur * 60;
 
 const char *mode;
 char temp[20];
 if (c->fmt.channels <= 2) {
 mode = c->fmt.channels == 1 ? _("Mono") : _("Stereo");
 }
 else {
 snprintf (temp, sizeof (temp), "%dch Multichannel", c->fmt.channels);
 mode = temp;
 }
 int samplerate = c->fmt.samplerate;
 int bitspersample = c->fmt.bps;
 songpos = playpos;
 //        codec_unlock ();
 
 char t[100];
 if (duration >= 0) {
 snprintf (t, sizeof (t), "%d:%02d", mindur, secdur);
 }
 else {
 strcpy (t, "-:--");
 }
 
 struct timeval tm;
 gettimeofday (&tm, NULL);
 if (tm.tv_sec - last_br_update.tv_sec + (tm.tv_usec - last_br_update.tv_usec) / 1000000.0 >= 0.3) {
 memcpy (&last_br_update, &tm, sizeof (tm));
 int bitrate = streamer_get_apx_bitrate ();
 if (bitrate > 0) {
 snprintf (sbitrate, sizeof (sbitrate), _("| %4d kbps "), bitrate);
 }
 else {
 sbitrate[0] = 0;
 }
 }
 const char *spaused = plug_get_output ()->state () == OUTPUT_STATE_PAUSED ? _("Paused | ") : "";
 const char *filetype = pl_find_meta (track, ":FILETYPE");
 if (!filetype) {
 filetype = "-";
 }
 snprintf (sbtext_new, sizeof (sbtext_new), _("%s%s %s| %dHz | %d bit | %s | %d:%02d / %s | %d tracks | %s total playtime"), spaused, filetype, sbitrate, samplerate, bitspersample, mode, minpos, secpos, t, deadbeef->pl_getcount (PL_MAIN), totaltime_str);
 }
 
 if (strcmp (sbtext_new, sb_text)) {
 strcpy (sb_text, sbtext_new);
 
 // form statusline
 // FIXME: don't update if window is not visible
 GtkStatusbar *sb = GTK_STATUSBAR (lookup_widget (mainwin, "statusbar"));
 if (sb_context_id == -1) {
 sb_context_id = gtk_statusbar_get_context_id (sb, "msg");
 }
 
 gtk_statusbar_pop (sb, sb_context_id);
 gtk_statusbar_push (sb, sb_context_id, sb_text);
 }
 
 if (mainwin) {
 GtkWidget *widget = lookup_widget (mainwin, "seekbar");
 // translate volume to seekbar pixels
 songpos /= duration;
 GtkAllocation a;
 gtk_widget_get_allocation (widget, &a);
 songpos *= a.width;
 if (fabs (songpos - last_songpos) > 0.01) {
 gtk_widget_queue_draw (widget);
 last_songpos = songpos;
 }
 }
 if (track) {
 pl_item_unref (track);
 }
 }
 */

+ (NSArray *) supportedSavePlaylistExtensions {
    
    NSMutableArray * array = [NSMutableArray arrayWithCapacity: 5];
    
    [array addObject: @"dbpl"];
    
    DB_playlist_t **plug = deadbeef->plug_get_playlist_list ();
    for (int i = 0; plug[i]; i++) {
        if (plug[i]->extensions && plug[i]->load) {
            const char **exts = plug[i]->extensions;
            if (exts && plug[i]->save) {
                for (int e = 0; exts[e]; e++) {
                    [array addObject: [NSString stringWithCString:exts[e] encoding:NSUTF8StringEncoding]];
                }
            }
        }
    }
    
    return array;
    
}

+ (NSArray *) supportedFormatsExtensions {
    
    NSMutableArray * array = [NSMutableArray arrayWithCapacity: 10];
	
	DB_decoder_t **codecs = deadbeef->plug_get_decoder_list ();
    for (int i = 0; codecs[i]; ++i) {
        if (codecs[i]->exts && codecs[i]->insert) {
            const char **exts = codecs[i]->exts;
            for (int e = 0; exts[e]; e++) {
                [array addObject: [NSString stringWithCString:exts[e] encoding:NSUTF8StringEncoding] ];
            }
        }
	}
	
	DB_vfs_t **vfsplugs = deadbeef->plug_get_vfs_list ();
    for (int i = 0; vfsplugs[i]; i++) {
        if (vfsplugs[i]->is_container) {
			const char **scheme_list = vfsplugs[i] -> get_schemes();			
			for(int e = 0; scheme_list[e]; ++e) {
				NSString * ext = [NSString stringWithCString:scheme_list[e] encoding:NSUTF8StringEncoding];
				// adds to the list removing the trailling "://"
				[array addObject: [ext substringToIndex: [ext rangeOfString:@":"].location] ];
			}
        }
    }
	
	return array;
}

+ (void) movePlayListItems : (NSIndexSet*) rowIndexes row:(NSInteger) rowBefore {
	
	deadbeef->pl_lock();
	ddb_playlist_t *plt = deadbeef->plt_get_curr ();
	
	int count = 0;
	DB_playItem_t * dropBefore = NULL;
	uint32_t indexes[count];
	int n = 0;
	
	count = [rowIndexes count];
	dropBefore = deadbeef->pl_get_for_idx(rowBefore);
	
	NSUInteger index = [rowIndexes firstIndex];
	while (index != NSNotFound) {
		indexes[n] = index;
		++n;
		index = [rowIndexes indexGreaterThanIndex:index ];
	}
	
	deadbeef->plt_move_items( (ddb_playlist_t *)plt, PL_MAIN, (ddb_playlist_t *)plt, dropBefore, indexes, count);
	
	deadbeef->plt_unref(plt);
	deadbeef->pl_unlock();
	deadbeef->pl_save_all();
}



+ (void) setCursor : (NSInteger) cursor {
	
    char conf[100];
    snprintf (conf, sizeof (conf), "playlist.cursor.%d", deadbeef->plt_get_curr_idx ());
    deadbeef->conf_set_int (conf, (int) cursor);
    return deadbeef->pl_set_cursor (PL_MAIN, (int) cursor);
}

+ (void) clearPlayList {
	
	deadbeef->pl_clear();
	deadbeef->pl_save_all();
}


+ (void) setVolumeDB:(float)value {
	
	deadbeef->volume_set_db(value);
}

+ (float) volumeDB {
	
	return deadbeef->volume_get_db();
	
}

+ (float) minVolumeDB {
	
	return deadbeef->volume_get_min_db();
}

+ (int) intConfiguration : (NSString *) key num:(NSInteger) def {
	
	return deadbeef->conf_get_int([key UTF8String], def);
}

+ (void) setIntConfiguration : (NSString *) key value:(NSInteger) def {
	
	deadbeef->conf_set_int([key UTF8String], def);
	deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
	
	return;
}

+ (NSString *) stringConfiguration : (NSString *) key str:(NSString *) def {
	
    deadbeef->conf_lock();
	NSString * str = [NSString stringWithUTF8String: deadbeef->conf_get_str_fast([key UTF8String], [def UTF8String]) ];
    deadbeef->conf_unlock();
    
    return str;
}

+ (void) setStringConfiguration : (NSString *) key value:(NSString *) def {
	
	deadbeef->conf_set_str([key UTF8String], [def UTF8String]);
	deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
	
	return;
}

+ (NSMutableDictionary *) keyList : (NSInteger) propertiesNumber {

	NSMutableDictionary * list = [NSMutableDictionary dictionaryWithCapacity:10];
	DB_playItem_t * it;
	DB_playItem_t ** tracks;
	int num_selected = 0;
	ddb_playlist_t *plt;
	
	deadbeef->pl_lock();
	
	plt = deadbeef->plt_get_curr ();
	num_selected = deadbeef->plt_getselcount(plt);
	
	if (0 < num_selected) {
        tracks = malloc (sizeof (DB_playItem_t *) * num_selected);
        if (tracks) {
            int n = 0;
            it = deadbeef->plt_get_first (plt, PL_MAIN);
            while (it) {
                if (deadbeef->pl_is_selected (it)) {
                    assert (n < num_selected);
                    deadbeef->pl_item_ref (it);
                    tracks[n++] = it;
                }
                DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
                deadbeef->pl_item_unref (it);
                it = next;
            }
        }
        else {
            deadbeef->pl_unlock ();
			deadbeef->plt_unref(plt);
            return list;
        }
    }
	
	for (int i = 0; i < num_selected; ++i)
	{
	    DB_metaInfo_t *meta = deadbeef->pl_get_metadata_head (tracks[i]);
        while (meta) {
			if (meta->key[0] != '!' && ((propertiesNumber && meta->key[0] == ':') || (!propertiesNumber && meta->key[0] != ':'))) {
				[list setObject:[NSString stringWithUTF8String: meta->value] forKey:[NSString stringWithUTF8String: meta->key] ];
			}
        	meta = meta -> next;
        }
		deadbeef->pl_item_unref (tracks[i]);
	}	
	
	deadbeef->pl_unlock();
	deadbeef->plt_unref(plt);
	return list;
}

+ (void) setCurrentPlaylist : (NSInteger) index {

    deadbeef->plt_set_curr_idx (index);
    deadbeef->conf_set_int ("playlist.current", index);
    
}

+ (void) setPlaylistItemsSelected : (NSIndexSet *) indexSet {
    
    deadbeef->pl_lock();
    ddb_playlist_t * plt = deadbeef -> plt_get_curr();

    deadbeef->plt_deselect_all (plt);
    
    [indexSet enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL *stop) {
       
        ddb_playItem_t * it = deadbeef->pl_get_for_idx ( (int) idx);
        deadbeef->pl_set_selected(it, 1);
        
    }];
    
    deadbeef->plt_unref(plt);
    deadbeef->pl_unlock();
    
}

+ (void) setPlaylistItemSelected : (NSInteger) index value:(BOOL) def {

	deadbeef->pl_lock();
	
	ddb_playlist_t * plt = deadbeef->plt_get_curr ();
	ddb_playItem_t * it = deadbeef->pl_get_for_idx ( (int) index ); // TODO: deprecated
	
	if(def)
		deadbeef->pl_set_selected(it, 1); // select
	else 
		deadbeef->pl_set_selected(it, 0); // unselect		
	
	
	deadbeef->plt_unref(plt);
	deadbeef->pl_unlock();
}

+ (NSMutableDictionary *) knownMetadataKeys {

	return [NSDictionary dictionaryWithObjectsAndKeys:
	 @"artist"	,	@"Artist",
	 @"title"	,	@"Track Title",
	 @"album"	,	@"Album",
	 @"year"	,	@"Date",
	 @"track"	,	@"Track Number",
	 @"numtracks",	@"Total Tracks",
	 @"genre"	,	@"Genre",
	 @"composer" ,	@"Composer",
	 @"disc"	,	@"Disc Number",
	 @"comment"	,	@"Comment",
	 nil];	
}

+ (void) updateSelectedTracksMetadata : (NSMutableDictionary *) metadata {
	
	DB_playItem_t * it;
	DB_playItem_t ** tracks;
	int num_selected = 0;
	ddb_playlist_t *plt;
	
	deadbeef->pl_lock();
	
	plt = deadbeef->plt_get_curr ();
	num_selected = deadbeef->plt_getselcount(plt);
	
	// fetch selected tracks
	if (0 < num_selected) {
        tracks = malloc (sizeof (DB_playItem_t *) * num_selected);
        if (tracks) {
            int n = 0;
            it = deadbeef->plt_get_first (plt, PL_MAIN);
            while (it) {
                if (deadbeef->pl_is_selected (it)) {
                    assert (n < num_selected);
                    deadbeef->pl_item_ref (it);
                    tracks[n++] = it;
                }
                DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
                deadbeef->pl_item_unref (it);
                it = next;
            }
        }
        else {
            deadbeef->pl_unlock ();
			deadbeef->plt_unref(plt);
			return;
        }
    }
	deadbeef->pl_unlock ();
	
	// update metadata
	for (int t =0; t < num_selected; ++t) {
	
		// TODO
		
	}
	
	// update data
	for (int t = 0; t < num_selected; t++) {

        DB_playItem_t *track = tracks[t];
        deadbeef->pl_lock ();
        const char *dec = deadbeef->pl_find_meta_raw (track, ":DECODER");
        char decoder_id[100];
        if (dec) {
            strncpy (decoder_id, dec, sizeof (decoder_id));
        }
        int match = track && dec;
        deadbeef->pl_unlock ();
        if (match) {
            int is_subtrack = deadbeef->pl_get_item_flags (track) & DDB_IS_SUBTRACK;
            if (is_subtrack) {
                continue;
            }
            deadbeef->pl_item_ref (track);
			// TODO: callback
            // find decoder
            DB_decoder_t *dec = NULL;
            DB_decoder_t **decoders = deadbeef->plug_get_decoder_list ();
            for (int i = 0; decoders[i]; i++) {
                if (!strcmp (decoders[i]->plugin.id, decoder_id)) {
                    dec = decoders[i];
                    if (dec->write_metadata) {
                        dec->write_metadata (track);
                    }
                    break;
                }
            }
			deadbeef->pl_item_unref (tracks[t]);
        }
    }
	
	deadbeef->plt_unref(plt);
	
}

+ (BOOL) addPathToPlaylistAtEnd : (NSString *) path {

	ddb_playlist_t *plt = deadbeef->plt_get_curr ();
    int  res = -1;
	
	if (!deadbeef->pl_add_files_begin (plt)) {
		res = deadbeef->plt_add_file (plt, [path UTF8String], NULL, NULL);
		deadbeef->pl_add_files_end ();
	} else {
		if (plt)
			deadbeef->plt_unref (plt);
		return NO;
	}
	
	if (plt)
		deadbeef->plt_unref (plt);
    
    if (res < 0)
        return NO;
	
	return YES;
}

+ (NSInteger) mainPlayListCount {

	return (NSInteger) deadbeef->pl_getcount(PL_MAIN);
}

+ (float) playingItemDuration {
	
	float duration = 0;
	
	DB_playItem_t *trk = deadbeef->streamer_get_playing_track ();
	if (!trk || deadbeef->pl_get_item_duration (trk) < 0) {
		duration = -1;
		if (trk)
			deadbeef->pl_item_unref(trk);
		return duration;
	}
	
	duration = deadbeef->pl_get_item_duration(trk);
	deadbeef->pl_item_unref(trk);
	
	return duration;
}

+ (float) playingItemPosition {

	return deadbeef->streamer_get_playpos();
}

+ (int) outputState {
	
	return deadbeef->get_output ()->state ();
}

+ (void) seekToPosition : (float) pos {
	
	deadbeef->sendmessage (DB_EV_SEEK, 0, pos, 0);
	return;
}

+ (NSDictionary *) pluginList {

	NSMutableDictionary * list = [[NSMutableDictionary alloc] init];
	char s[20];
	
	DB_plugin_t **plugins = deadbeef -> plug_get_list();
	for (int i = 0; plugins[i]; ++i)
	{
		snprintf (s, sizeof (s), "%d.%d", plugins[i] -> version_major, plugins[i] -> version_minor);
		[list setObject:[NSArray arrayWithObjects: 
						 [NSString stringWithUTF8String: plugins[i]->descr],
						 [NSString stringWithUTF8String: plugins[i]->copyright],
						 [NSString stringWithUTF8String: plugins[i]->website],
						 [NSString stringWithUTF8String: s],
						 nil ] 
				 forKey: [NSString stringWithUTF8String: plugins[i]->name] ];
	}
	
	return list;
}

+ (NSArray *) outputPluginList {

    DB_output_t ** plugins = deadbeef -> plug_get_output_list ();
    NSMutableArray * list = [NSMutableArray arrayWithCapacity:2];
    
    for (int i = 0; plugins[i]; ++i) {
        [list addObject: [NSString stringWithUTF8String: plugins[i]->plugin.name]];
    }
    
    return list;
}

+ (NSArray *) availablePlaylists {

	int count = deadbeef-> plt_get_count();
    NSMutableArray * list = [NSMutableArray arrayWithCapacity: count];
    NSDictionary * item;

    deadbeef->pl_lock();
	for (int i = 0; i < count; ++i)
	{
        char title[1000];
        ddb_playlist_t *plt = deadbeef->plt_get_for_idx (i);
        deadbeef->plt_get_title (plt, title, sizeof (title));
        deadbeef->plt_unref (plt);

        item = [NSMutableDictionary dictionaryWithObjectsAndKeys: [NSString stringWithUTF8String: title], @"name", nil];
        [list addObject: item];
	}

    deadbeef->pl_unlock();
    
    return list;
}

+ (NSArray *) replaygainModeList {

	return [NSArray arrayWithObjects: NSLocalizedString(@"Disable", "Disable"), NSLocalizedString(@"Track", "Track"), NSLocalizedString(@"Album", "Album"), nil ];
}

+ (NSArray *) proxyTypeList {
    
    return [NSArray arrayWithObjects: @"HTTP", @"HTTP_1_0", @"SOCKS4", @"SOCKS5", @"SOCKS4A", @"SOCKS5_HOSTNAME", nil];
}

+ (NSInteger) currentPlaylistIndex {
    return deadbeef->plt_get_curr_idx ();
}

+ (void) loadPlaylist: (NSURL *) url
{
    NSString * str = [url path];
    const char * fname = [str UTF8String];
    [DBAppDelegate setStringConfiguration:@"filechooser.playlist.lastdir" value: [str stringByDeletingLastPathComponent] ];
    
    if (fname) {
    
        // the add files code is executed in another thread
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
        
            ddb_playlist_t *plt = deadbeef->plt_get_curr ();
            if (plt) {
                if (!deadbeef->plt_add_files_begin (plt, 0)) {
                    deadbeef->plt_clear (plt);
                    int abort = 0;
                    DB_playItem_t *it = deadbeef->plt_load2 (0, plt, NULL, fname, &abort, NULL, NULL);
                    deadbeef->plt_save_config (plt);
                    deadbeef->plt_add_files_end (plt, 0);
                }
                deadbeef->plt_unref (plt);
            }
            deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, 0, 0);
            
        });
    }
    
}

+ (BOOL) saveCurrentPlaylist: (NSURL *) url {
    
    const char * fname = [[url path] UTF8String];
    
    if (fname) {
        
        dispatch_async(dispatch_get_global_queue(0, 0),
                       ^ {
                           
            ddb_playlist_t *plt = deadbeef->plt_get_curr ();
            if (plt) {
                int res = deadbeef->plt_save (plt, NULL, NULL, fname, NULL, NULL, NULL);
                if (res >= 0 && strlen (fname) < 1024) {
                    ;
                }
                deadbeef->plt_unref (plt);
            }
        
        });
        
        return YES;
        
    }
    
    return NO;
}

+ (BOOL) newPlaylist {
    
    int cnt = deadbeef->plt_get_count ();
    int i;
    int idx = 0;
    int pl = -1;
    
    for (;;) {
        char name[100];
        if (!idx) {
            strcpy (name, [NSLocalizedString(@"New Playlist", "new playlist") UTF8String]);
        }
        else {
            snprintf (name, sizeof (name), [NSLocalizedString(@"New Playlist (%d)", "new playlist seq") UTF8String], idx);
        }
        deadbeef->pl_lock ();
        for (i = 0; i < cnt; i++) {
            char t[100];
            ddb_playlist_t *plt = deadbeef->plt_get_for_idx (i);
            deadbeef->plt_get_title (plt, t, sizeof (t));
            deadbeef->plt_unref (plt);
            if (!strcasecmp (t, name)) {
                break;
            }
        }
        deadbeef->pl_unlock ();
        if (i == cnt) {
            pl = deadbeef->plt_add (cnt, name);
            break;
        }
        idx++;
    }

    if (pl != -1) {
        deadbeef->plt_set_curr_idx (pl);
        [DBAppDelegate setIntConfiguration:@"playlist.current" value: pl];
    }
    
    return YES;

}

+ (NSInteger) streamingTrackIndex {
    
    DB_playItem_t * streamingTrack = deadbeef->streamer_get_streaming_track();
    NSInteger index = 0;
    
	if (streamingTrack <= 0) {
		return -1;
	}
    
    index = (NSInteger) deadbeef->pl_get_idx_of(streamingTrack);
    
    if (streamingTrack)
        deadbeef->pl_item_unref (streamingTrack);
	
    return index;
}

+ (BOOL) streamerOkToRead {
    if (deadbeef->streamer_ok_to_read (-1) == 1)
        return YES;

    return NO;
}

@end
