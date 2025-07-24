package com.icelater.anticheat;

import com.icelater.anticheat.commands.ACCommand;
import com.icelater.anticheat.checks.CheckManager;
import org.bukkit.Bukkit;
import org.bukkit.plugin.java.JavaPlugin;

public class AdvancedAntiCheat extends JavaPlugin {
    private static AdvancedAntiCheat instance;
    private CheckManager checkManager;

    @Override
    public void onEnable() {
        instance = this;
        saveDefaultConfig();
        checkManager = new CheckManager(this);
        checkManager.registerAll();
        getCommand("anticheat").setExecutor(new ACCommand());
        getLogger().info("AdvancedAntiCheat enabled!");
    }

    @Override
    public void onDisable() {
        getLogger().info("AdvancedAntiCheat disabled!");
    }

    public static AdvancedAntiCheat getInstance() {
        return instance;
    }

    public CheckManager getCheckManager() {
        return checkManager;
    }
}
