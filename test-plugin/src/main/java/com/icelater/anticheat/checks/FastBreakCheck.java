package com.icelater.anticheat.checks;

import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.block.BlockDamageEvent;

public class FastBreakCheck implements Check {
    private double maxSpeed;
    private int threshold;

    @Override
    public String getName() { return "FastBreakCheck"; }

    @Override
    public boolean isEnabled() { return maxSpeed > 0; }

    @Override
    public void configure(CheckConfig config) {
        this.maxSpeed = config.get("max-speed", Double.class);
        this.threshold = config.get("threshold", Integer.class);
    }

    @EventHandler
    public void onBreak(BlockDamageEvent e) {
        Player p = e.getPlayer();
        if (p.hasPermission("anticheat.bypass")) return;
        long now = System.currentTimeMillis();
        long last = CheckManagerUtils.getLastBreak(p);
        if (now - last < maxSpeed * 1000) {
            CheckManagerUtils.flag(p, getName());
            e.setCancelled(true);
        }
        CheckManagerUtils.updateLastBreak(p, now);
    }
        }
