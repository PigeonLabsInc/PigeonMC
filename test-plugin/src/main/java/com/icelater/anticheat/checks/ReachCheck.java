package com.icelater.anticheat.checks;

import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.player.PlayerInteractEntityEvent;
import org.bukkit.util.Vector;

public class ReachCheck implements Check {
    private double maxReach;
    private int threshold;

    @Override
    public String getName() { return "ReachCheck"; }

    @Override
    public boolean isEnabled() { return maxReach > 0; }

    @Override
    public void configure(CheckConfig config) {
        this.maxReach = config.get("max-reach", Double.class);
        this.threshold = config.get("threshold", Integer.class);
    }

    @EventHandler
    public void onInteract(PlayerInteractEntityEvent e) {
        Player p = e.getPlayer();
        if (p.hasPermission("anticheat.bypass")) return;
        Vector eye = p.getEyeLocation().toVector();
        Vector target = e.getRightClicked().getLocation().add(0,1,0).toVector();
        if (eye.distance(target) > maxReach) {
            e.setCancelled(true);
            CheckManagerUtils.flag(p, getName());
        }
    }
              }
