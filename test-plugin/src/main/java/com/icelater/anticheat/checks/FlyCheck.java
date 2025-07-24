package com.icelater.anticheat.checks;

import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.player.PlayerMoveEvent;
import org.bukkit.plugin.java.JavaPlugin;

public class FlyCheck implements Check {
    private double maxHeight;
    private int threshold;

    @Override
    public String getName() { return "FlyCheck"; }

    @Override
    public boolean isEnabled() { return maxHeight > 0; }

    @Override
    public void configure(CheckConfig config) {
        this.maxHeight = config.get("max-height", Double.class);
        this.threshold = config.get("threshold", Integer.class);
    }

    @EventHandler
    public void onMove(PlayerMoveEvent e) {
        Player p = e.getPlayer();
        if (p.hasPermission("anticheat.bypass")) return;
        double yDiff = e.getTo().getY() - e.getFrom().getY();
        if (yDiff > maxHeight) {
            CheckManagerUtils.flag(p, getName());
            p.teleport(e.getFrom());
        }
    }
}
