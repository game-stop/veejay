#!/usr/bin/env perl

# feed this script with vims.h to generate selectors.h
# Emits canonical VIMS aliases plus common Pure Data shortcuts.
# VIMS_MAX is a sentinel, and VIMS ids 400..499 return command-socket
# payloads which sendVIMS does not parse, so they are intentionally skipped.

my %id = ();
my %seen_alias = ();
my @order = ();

while (<>) {
    if (m/\b(VIMS_\S+)\s*=\s*(\d+)\s*,?/) {
        my $sym = $1;
        my $num = int($2);
        $id{$sym} = $num;
        push @order, $sym;
    }
}

print "// selectors.h\n";
print "// generated from vims.h plus common Pure Data aliases\n";
print "// canonical aliases are VIMS_* names lowercased with underscores changed to dots\n";
print "// VIMS_MAX and reply/getter ids 400..499 are intentionally not emitted\n";
print "// valid raw event ports are p001..p399 and p500..p601\n";

sub emit_alias {
    my ($alias, $num) = @_;
    return if $alias eq "";
    return if $seen_alias{$alias};
    return if $num <= 0 || $num >= 602;
    return if $num >= 400 && $num < 500;
    $seen_alias{$alias} = 1;
    print "SELECTOR(\"" . $alias . "\", " . $num . ");\n";
}

foreach my $sym (@order) {
    next if $sym eq "VIMS_MAX";
    my $tag = lc $sym;
    $tag =~ s/^vims_//;
    $tag =~ s/_/./g;
    emit_alias($tag, $id{$sym});
}

my @common = (
    ["play", "VIMS_VIDEO_PLAY_FORWARD"],
    ["forward", "VIMS_VIDEO_PLAY_FORWARD"],
    ["reverse", "VIMS_VIDEO_PLAY_BACKWARD"],
    ["backward", "VIMS_VIDEO_PLAY_BACKWARD"],
    ["stop", "VIMS_VIDEO_PLAY_STOP"],
    ["stop.all", "VIMS_VIDEO_PLAY_STOP_ALL"],
    ["freeze", "VIMS_VIDEO_SET_FREEZE"],
    ["frame", "VIMS_VIDEO_SET_FRAME"],
    ["speed", "VIMS_VIDEO_SET_SPEED"],
    ["speed.k", "VIMS_VIDEO_SET_SPEEDK"],
    ["slow", "VIMS_VIDEO_SET_SLOW"],
    ["percent", "VIMS_VIDEO_SET_FRAME_PERCENTAGE"],
    ["goto.start", "VIMS_VIDEO_GOTO_START"],
    ["goto.end", "VIMS_VIDEO_GOTO_END"],
    ["home", "VIMS_VIDEO_GOTO_START"],
    ["end", "VIMS_VIDEO_GOTO_END"],
    ["skip.frame", "VIMS_VIDEO_SKIP_FRAME"],
    ["prev.frame", "VIMS_VIDEO_PREV_FRAME"],
    ["skip.second", "VIMS_VIDEO_SKIP_SECOND"],
    ["prev.second", "VIMS_VIDEO_PREV_SECOND"],
    ["fps", "VIMS_FRAMERATE"],
    ["mode", "VIMS_SET_PLAY_MODE"],
    ["plain", "VIMS_SET_PLAIN_MODE"],
    ["switch", "VIMS_SWITCH_SAMPLE_STREAM"],
    ["select", "VIMS_SELECT_ID"],
    ["bank", "VIMS_SELECT_BANK"],
    ["sample", "VIMS_SAMPLE_SELECT"],
    ["sample.next", "VIMS_SAMPLE_NEXT"],
    ["sample.start.here", "VIMS_SET_SAMPLE_START"],
    ["sample.end.here", "VIMS_SET_SAMPLE_END"],
    ["sample.loop", "VIMS_SAMPLE_TOGGLE_LOOP"],
    ["sample.rand.loop", "VIMS_SAMPLE_TOGGLE_RAND_LOOP"],
    ["sample.position", "VIMS_SAMPLE_SET_POSITION"],
    ["sample.volume", "VIMS_SAMPLE_SET_VOLUME"],
    ["hold", "VIMS_SAMPLE_HOLD_FRAME"],
    ["stream", "VIMS_STREAM_SELECT"],
    ["stream.on", "VIMS_STREAM_ACTIVATE"],
    ["stream.off", "VIMS_STREAM_DEACTIVATE"],
    ["rec.start", "VIMS_REC_START"],
    ["rec.stop", "VIMS_REC_STOP"],
    ["rec.auto", "VIMS_REC_AUTO_START"],
    ["mute", "VIMS_AUDIO_TOGGLE_MUTE"],
    ["audio.mute", "VIMS_AUDIO_TOGGLE_MUTE"],
    ["audio.on", "VIMS_AUDIO_ENABLE"],
    ["audio.off", "VIMS_AUDIO_DISABLE"],
    ["volume", "VIMS_SET_VOLUME"],
    ["record.audio.source", "VIMS_RECORD_AUDIO_SOURCE"],
    ["beat", "VIMS_AUDIO_BEAT_TOGGLE"],
    ["beat.toggle", "VIMS_AUDIO_BEAT_TOGGLE"],
    ["beat.status", "VIMS_AUDIO_BEAT_STATUS"],
    ["beat.config", "VIMS_AUDIO_BEAT_CONFIG"],
    ["beat.action", "VIMS_AUDIO_BEAT_ACTION"],
    ["beat.pulse", "VIMS_AUDIO_BEAT_PULSE"],
    ["beat.gate", "VIMS_AUDIO_BEAT_GATE"],
    ["beat.auto.mode", "VIMS_AUDIO_BEAT_AUTO_MODE"],
    ["beat.auto.amount", "VIMS_AUDIO_BEAT_AUTO_AMOUNT"],
    ["beat.auto.reset", "VIMS_AUDIO_BEAT_AUTO_RESET"],
    ["beat.state", "VIMS_AUDIO_BEAT_STATE"],
    ["beat.freeze", "VIMS_AUDIO_BEAT_FREEZE"],
    ["beat.cooldown", "VIMS_AUDIO_BEAT_COOLDOWN"],
    ["beat.threshold", "VIMS_AUDIO_BEAT_THRESHOLD"],
    ["beat.channels", "VIMS_AUDIO_BEAT_CHANNELS"],
    ["beat.print", "VIMS_AUDIO_BEAT_PRINT"],
    ["beat.scratch", "VIMS_AUDIO_BEAT_SCRATCH_SENSITIVITY"],
    ["beat.pause.on.loss", "VIMS_AUDIO_BEAT_SOURCE_LOSS_PAUSE"],
    ["beat.monitor.latency", "VIMS_AUDIO_BEAT_MONITOR_LATENCY"],
    ["sync.status", "VIMS_AUDIO_SYNC_STATUS"],
    ["sync.mode", "VIMS_AUDIO_SYNC_MODE"],
    ["sync.jack", "VIMS_AUDIO_SYNC_JACK"],
    ["sync.wav", "VIMS_AUDIO_SYNC_WAV"],
    ["sync.target", "VIMS_AUDIO_SYNC_TARGET"],
    ["sync.correction", "VIMS_AUDIO_SYNC_CORRECTION"],
    ["sync.print", "VIMS_AUDIO_SYNC_PRINT"],
    ["mix.mode", "VIMS_AUDIO_MIX_MODE"],
    ["mix.crossfade", "VIMS_AUDIO_MIX_CROSSFADE"],
    ["crossfade", "VIMS_AUDIO_MIX_CROSSFADE"],
    ["fx.prev", "VIMS_FXLIST_INC"],
    ["fx.next", "VIMS_FXLIST_DEC"],
    ["fx.add", "VIMS_FXLIST_ADD"],
    ["fx.bg", "VIMS_EFFECT_SET_BG"],
    ["entry.up", "VIMS_CHAIN_ENTRY_UP"],
    ["entry.down", "VIMS_CHAIN_ENTRY_DOWN"],
    ["entry.channel.inc", "VIMS_CHAIN_ENTRY_CHANNEL_INC"],
    ["entry.channel.dec", "VIMS_CHAIN_ENTRY_CHANNEL_DEC"],
    ["entry.source.toggle", "VIMS_CHAIN_ENTRY_SOURCE_TOGGLE"],
    ["entry.arg.inc", "VIMS_CHAIN_ENTRY_INC_ARG"],
    ["entry.arg.dec", "VIMS_CHAIN_ENTRY_DEC_ARG"],
    ["entry.arg.set", "VIMS_CHAIN_ENTRY_SET_ARG_VAL"],
    ["entry.narg.set", "VIMS_CHAIN_ENTRY_SET_NARG_VAL"],
    ["entry.effect", "VIMS_CHAIN_ENTRY_SET_EFFECT"],
    ["entry.preset", "VIMS_CHAIN_ENTRY_SET_PRESET"],
    ["entry.video.on", "VIMS_CHAIN_ENTRY_SET_VIDEO_ON"],
    ["entry.video.off", "VIMS_CHAIN_ENTRY_SET_VIDEO_OFF"],
    ["entry.defaults", "VIMS_CHAIN_ENTRY_SET_DEFAULTS"],
    ["entry.channel", "VIMS_CHAIN_ENTRY_SET_CHANNEL"],
    ["entry.source", "VIMS_CHAIN_ENTRY_SET_SOURCE"],
    ["entry.source.channel", "VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL"],
    ["entry.clear", "VIMS_CHAIN_ENTRY_CLEAR"],
    ["entry.state", "VIMS_CHAIN_ENTRY_SET_STATE"],
    ["entry.beat", "VIMS_CHAIN_ENTRY_BEAT_TOGGLE"],
    ["chain.on", "VIMS_CHAIN_ENABLE"],
    ["chain.off", "VIMS_CHAIN_DISABLE"],
    ["chain.clear", "VIMS_CHAIN_CLEAR"],
    ["chain.toggle", "VIMS_CHAIN_TOGGLE"],
    ["chain.all", "VIMS_CHAIN_TOGGLE_ALL"],
    ["chain.fade.in", "VIMS_CHAIN_FADE_IN"],
    ["chain.fade.out", "VIMS_CHAIN_FADE_OUT"],
    ["chain.fade.alpha", "VIMS_CHAIN_FADE_ALPHA"],
    ["global.chain", "VIMS_GLOBAL_CHAIN"],
    ["seq", "VIMS_SEQUENCE_STATUS"],
    ["seq.play", "VIMS_SEQUENCE_STATUS"],
    ["seq.add", "VIMS_SEQUENCE_ADD"],
    ["seq.del", "VIMS_SEQUENCE_DEL"],
    ["seq.select", "VIMS_SEQUENCE_SELECT"],
    ["seq.copy", "VIMS_SEQUENCE_COPY"],
    ["seq.clear", "VIMS_SEQUENCE_CLEAR_ALL"],
    ["screenshot", "VIMS_SCREENSHOT"],
    ["feedback", "VIMS_FEEDBACK"],
    ["forwarding", "VIMS_MESSAGE_FORWARDING"],
);

foreach my $pair (@common) {
    my ($alias, $sym) = @$pair;
    next unless exists $id{$sym};
    emit_alias($alias, $id{$sym});
}
