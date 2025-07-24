package com.icelater.anticheat.checks;

import org.bukkit.entity.Player;
import org.bukkit.event.Listener;

public interface Check extends Listener {
    String getName();
    boolean isEnabled();
    void configure(CheckConfig config);
}
