# Patch-overlapping Java context after the fix

## `src/main/java/com/parasoft/environmentmanager/jenkins/EnvironmentManagerBuilder.java` (changed lines (67, 104, 171, 172, 173, 174, 175, 176, 177))

```java
/*
 * Copyright 2016 Parasoft Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.parasoft.environmentmanager.jenkins;

import java.io.IOException;

import net.sf.json.JSONArray;
import net.sf.json.JSONObject;

import org.kohsuke.stapler.DataBoundConstructor;
import org.kohsuke.stapler.QueryParameter;
import org.kohsuke.stapler.StaplerRequest;

import com.parasoft.em.client.api.EnvironmentCopy;
import com.parasoft.em.client.api.Environments;
import com.parasoft.em.client.api.EventMonitor;
import com.parasoft.em.client.api.Provisions;
import com.parasoft.em.client.api.Servers;
import com.parasoft.em.client.api.Systems;
import com.parasoft.em.client.impl.EnvironmentCopyImpl;
import com.parasoft.em.client.impl.EnvironmentsImpl;
import com.parasoft.em.client.impl.ProvisionsImpl;
import com.parasoft.em.client.impl.ServersImpl;
import com.parasoft.em.client.impl.SystemsImpl;
import com.parasoft.environmentmanager.jenkins.EnvironmentManagerPlugin.EnvironmentManagerPluginDescriptor;

import hudson.EnvVars;
import hudson.Extension;
import hudson.Launcher;
import hudson.model.AbstractBuild;
import hudson.model.AbstractProject;
import hudson.model.BuildListener;
import hudson.tasks.BuildStepDescriptor;
import hudson.tasks.Builder;
import hudson.util.Secret;
import hudson.util.ListBoxModel;

public class EnvironmentManagerBuilder extends Builder {
    private int systemId;
    private int environmentId;
    private int instanceId;
    private boolean copyToServer;
    private String newEnvironmentName;
    private String serverType;
    private int serverId;
    private String serverHost;
    private String serverName;
    private boolean copyDataRepo;
    private String repoType;
    private String repoHost;
    private int repoPort;
    private String repoUsername;
    private Secret repoPassword;
    private boolean abortOnFailure;
    
    @DataBoundConstructor
    public EnvironmentManagerBuilder(
        int systemId,
        int environmentId,
        int instanceId,
        boolean copyToServer,
        String newEnvironmentName,
        String serverType,
        int serverId,
        String serverHost,
        String serverName,
        boolean copyDataRepo,
        String repoType,
        String repoHost,
        int repoPort,
        String repoUsername,
        String repoPassword,
        boolean abortOnFailure)
    {
        super();
        this.systemId = systemId;
        this.environmentId = environmentId;
        this.instanceId = instanceId;
        this.copyToServer = copyToServer;
        this.newEnvironmentName = newEnvironmentName;
        this.serverType = serverType;
        this.serverId = serverId;
        this.serverHost = serverHost;
        this.serverName = serverName;
        this.copyDataRepo = copyDataRepo;
        this.repoType = repoType;
        this.repoHost = repoHost;
        this.repoPort = repoPort;
        this.repoUsername = repoUsername;
        this.repoPassword = Secret.fromString(repoPassword);
        this.abortOnFailure = abortOnFailure;
    }
    
    public int getSystemId() {
        return systemId;
    }
    
    public int getEnvironmentId() {
        return environmentId;
    }
    
    public int getInstanceId() {
        return instanceId;
    }
    
    public boolean isCopyToServer() {
        return copyToServer;
    }
    
    public String getNewEnvironmentName() {
        return newEnvironmentName;
    }
    
    public boolean isServerType(String type) {
        if ((serverType == null) || serverType.isEmpty()) {
            return "registered".equals(type);
        }
        return serverType.equals(type);
    }
    
    public int getServerId() {
        return serverId;
    }
    
    public String getServerHost() {
        return serverHost;
    }
    
    public String getServerName() {
        return serverName;
    }
    
    public boolean isCopyDataRepo() {
        return copyDataRepo;
    }
    
    public boolean isRepoType(String type) {
        if ((repoType == null) || repoType.isEmpty()) {
            return "current".equals(type);
        }
        return repoType.equals(type);
    }
    
    public String getRepoHost() {
        return repoHost;
    }
    
    public int getRepoPort() {
        return repoPort == 0 ? 2424 : repoPort;
    }
    
    public String getRepoUsername() {
        return repoUsername == null ? "admin" : repoUsername;
    }
    
    public String getRepoPassword() {
        if (repoPassword != null) {
            String plainText = repoPassword.getPlainText();
            if (!plainText.isEmpty()) {
                return plainText;
            }
        }
        return "admin";
    }
    
    public boolean isAbortOnFailure() {
        return abortOnFailure;
    }
    
    @Override
    public boolean perform(final AbstractBuild<?, ?> build, Launcher launcher,
            final BuildListener listener) throws InterruptedException, IOException
    {
        EnvironmentManagerPluginDescriptor pluginDescriptor =
            EnvironmentManagerPlugin.getEnvironmentManagerPluginDescriptor();
        String emUrl = pluginDescriptor.getEmUrl();
        String username = pluginDescriptor.getUsername();
        Secret password = pluginDescriptor.getPassword();
        EnvVars envVars = build.getEnvironment(listener);
        int targetEnvironmentId = environmentId;
        int targetInstanceId = instanceId;
        Environments environments = new EnvironmentsImpl(emUrl, username, password.getPlainText());
        JSONObject instance = environments.getEnvironmentInstance(environmentId, instanceId);
        if (copyToServer) {
            JSONObject targetServer = null;
            int targetServerId = 0;
            String targetServerName = envVars.expand(serverName);
            String targetServerHost = envVars.expand(serverHost);
            boolean waitingNotFoundMessageShown = false;
            boolean waitingOfflineMessageShown = false;
            String status = null;
            while (targetServerId == 0) {
                if (isServerType("registered")) {
                    targetServerId = serverId;
                }
                Servers servers = new ServersImpl(emUrl, username, password.getPlainText());
                JSONObject response = servers.getServers();
                if (response.has("servers")) {
                    JSONArray envArray = response.getJSONArray("servers");
                    for (Object o : envArray) {
                        JSONObject server = (JSONObject) o;
                        if (isServerType("registered")) {
                            int id = server.getInt("id");
                            if (id == serverId) {
                                targetServer = server;
                                status = server.optString("status");
                            }
                        } else if (isServerType("name")) {
                            String name = server.getString("name");
                            if (name.indexOf(targetServerName) >= 0) {
                                targetServerId = server.getInt("id");
                                targetServer = server;
                                status = server.optString("status");
                            }
                            if (name.equalsIgnoreCase(targetServerName)) {
                                break;
                            }
                        } else if (isServerType("host")) {
                            String host = server.getString("host");
                            if (host.indexOf(targetServerHost) >= 0) {
                                targetServerId = server.getInt("id");
                                targetServer = server;
                                status = server.optString("status");
                            }
                            if (host.equalsIgnoreCase(targetServerHost)) {
                                break;
                            }
                        }
                    }
                }
                if ((targetServerId == 0) && !waitingNotFoundMessageShown) {
                    String errorMessage = "WARNING:  Could not find any Virtualize servers matching ";
                    if (isServerType("name")) {
                        errorMessage += "name:  " + targetServerName;
                    } else if (isServerType("host")) {
                        errorMessage += "host:  " + targetServerHost;
                    }
                    listener.getLogger().println(errorMessage);
                    listener.getLogger().println("Waiting for a matching Virtualize server to register with the Continuous Testing Platform...");
                    waitingNotFoundMessageShown = true;
                }
                if ("OFFLINE".equals(status) || "REFRESHING".equals(status)) {
                    targetServerId = 0;
                    if (!waitingOfflineMessageShown) {
                        listener.getLogger().println("Waiting for Virtualize server to come online...");
                        waitingOfflineMessageShown = true;
                    }
                }
                Thread.sleep(10000); // try again in 10 seconds
            }
            EnvironmentCopy environmentCopy = new EnvironmentCopyImpl(emUrl, username, password.getPlainText());
            JSONObject dataRepoSettings = null;
            if (isRepoType("target")) {
                dataRepoSettings = new JSONObject();
                dataRepoSettings.put("host", targetServer.getString("host"));
            } else if (isRepoType("custom")) {
                dataRepoSettings = new JSONObject();
                dataRepoSettings.put("host", getRepoHost());
            }
            if (dataRepoSettings != null) {
                dataRepoSettings.put("port", getRepoPort());
                dataRepoSettings.put("username", getRepoUsername());
                dataRepoSettings.put("password", getRepoPassword());
            }
            JSONObject copyEvent = environmentCopy.createEnvironmentCopy(environmentId, targetServerId, envVars.expand(newEnvironmentName), copyDataRepo, dataRepoSettings);
            boolean copyResult = environmentCopy.monitorEvent(copyEvent, new EventMonitor() {
                public void logMessage(String message) {
                    listener.getLogger().println(message);
                }
            });
            JSONObject copyStatus = environmentCopy.removeCopyStatus(copyEvent.getInt("id"));
            if (!copyResult) {
                return false;
            }
            targetEnvironmentId = copyStatus.getInt("environmentId");
            targetInstanceId = 0;
            String instanceName = instance.getString("name");
            JSONObject copiedInstances = environments.getEnvironmentInstances(targetEnvironmentId);
            if (copiedInstances.has("instances")) {
                JSONArray instArray = copiedInstances.getJSONArray("instances");
                for (int i = 0; i < instArray.size(); i++) {
                    JSONObject inst = instArray.getJSONObject(i);
                    if (instanceName.equals(inst.getString("name"))) {
                        targetInstanceId = inst.getInt("id");
                        break;
                    }
                }
            }
            if (targetInstanceId == 0) {
                listener.getLogger().println("Unable to find environment instance named \"" + instanceName + "\" in the copied environment.");
                return false;
            }
        }
        listener.getLogger().println("Executing provisioning action on " + emUrl);
        Provisions provisions = new ProvisionsImpl(emUrl, username, password.getPlainText());
        JSONObject event = provisions.createProvisionEvent(targetEnvironmentId, targetInstanceId, abortOnFailure);
        boolean result = provisions.monitorEvent(event, new EventMonitor() {
            public void logMessage(String message) {
                listener.getLogger().println(message);
            }
        });
        String baseUrl = emUrl;
        if (!baseUrl.endsWith("/")) {
            baseUrl += "/";
        }
        
        JSONObject eventResult = provisions.getProvisions(event.getInt("eventId"));
        JSONArray steps = eventResult.getJSONArray("steps");
        int failed = 0;
        for (int i = 0; i < steps.size(); i++) {
            JSONObject step = steps.getJSONObject(i);
            if ("error".equals(step.getString("result"))) {
                failed++;
            }
        }
        
        String environmentUrl = baseUrl + "environments/" + targetEnvironmentId;
        build.addAction(new ProvisioningEventAction(build, instance.getString("name"), environmentUrl, steps.size(), failed));
        return result;
    }
    
    @Override
    public DescriptorImpl getDescriptor() {
        return (DescriptorImpl)super.getDescriptor();
    }
    
    @Extension
    public static final class DescriptorImpl extends BuildStepDescriptor<Builder> {

        public DescriptorImpl() {
            load();
        }

        @Override
        public boolean isApplicable(Class<? extends AbstractProject> jobType) {
            return true;
        }

        @Override
        public String getDisplayName() {
            return "Deploy an environment";
        }

        @Override
        public boolean configure(StaplerRequest req, JSONObject json) throws FormException {
            req.bindJSON(this, json);
            save();
            return super.configure(req, json);
        }
        
        public ListBoxModel doFillSystemIdItems() {
            ListBoxModel m = new ListBoxModel();
            try {
                EnvironmentManagerPluginDescriptor pluginDescriptor =
                    EnvironmentManagerPlugin.getEnvironmentManagerPluginDescriptor();
                String emUrl = pluginDescriptor.getEmUrl();
                String username = pluginDescriptor.getUsername();
                Secret password = pluginDescriptor.getPassword();
                if (emUrl != null) {
                    Systems systems = new SystemsImpl(emUrl, username, password.getPlainText());
                    JSONObject envs = systems.getSystems();
                    if (envs.has("systems")) {
                        JSONArray sysArray = envs.getJSONArray("systems");
                        for (Object o : sysArray) {
                            JSONObject system = (JSONObject) o;
                            String name = system.getString("name");
                            if (system.has("version")) {
                                name += " (" + system.getString("version") + ")";
                            }
                            m.add(name, system.getString("id"));
                        }
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
            return m;
        }
        
        public ListBoxModel doFillEnvironmentIdItems(@QueryParameter int systemId) {
            ListBoxModel m = new ListBoxModel();
            try {
                EnvironmentManagerPluginDescriptor pluginDescriptor =
                    EnvironmentManagerPlugin.getEnvironmentManagerPluginDescriptor();
                String emUrl = pluginDescriptor.getEmUrl();
                String username = pluginDescriptor.getUsername();
                Secret password = pluginDescriptor.getPassword();
                if (emUrl != null) {
                    if (systemId == 0) {
                        Systems systems = new SystemsImpl(emUrl, username, password.getPlainText());
                        JSONObject envs = systems.getSystems();
                        if (envs.has("systems")) {
                            JSONArray sysArray = envs.getJSONArray("systems");
                            if (sysArray.size() > 0) {
                                JSONObject system = sysArray.getJSONObject(0);
                                systemId = system.getInt("id");
                            }
                        }
                    }
                    Environments environments = new EnvironmentsImpl(emUrl, username, password.getPlainText());
                    JSONObject envs = environments.getEnvironments();
                    if (envs.has("environments")) {
                        JSONArray envArray = envs.getJSONArray("environments");
                        for (Object o : envArray) {
                            JSONObject env = (JSONObject) o;
                            if (env.getInt("systemId") == systemId) {
                                String name = env.getString("name");
                                if (env.has("version")) {
                                    name += " (" + env.getString("version") + ")";
                                }
                                m.add(name, env.getString("id"));
                            }
                        }
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
            return m;
        }
        
        public ListBoxModel doFillInstanceIdItems(@QueryParameter int systemId, @QueryParameter int environmentId) {
            ListBoxModel m = new ListBoxModel();
            try {
                EnvironmentManagerPluginDescriptor pluginDescriptor =
                    EnvironmentManagerPlugin.getEnvironmentManagerPluginDescriptor();
                String emUrl = pluginDescriptor.getEmUrl();
                String username = pluginDescriptor.getUsername();
                Secret password = pluginDescriptor.getPassword();
                if (systemId == 0) {
                    Systems systems = new SystemsImpl(emUrl, username, password.getPlainText());
                    JSONObject envs = systems.getSystems();
                    if (envs.has("systems")) {
                        JSONArray sysArray = envs.getJSONArray("systems");
                        if (sysArray.size() > 0) {
                            JSONObject system = sysArray.getJSONObject(0);
                            systemId = system.getInt("id");
                        }
                    }
                }
                Environments environments = new EnvironmentsImpl(emUrl, username, password.getPlainText());
                if (environmentId == 0) {
                    JSONObject envs = environments.getEnvironments();
                    if (envs.has("environments")) {
                        JSONArray envArray = envs.getJSONArray("environments");
                        for (Object o : envArray) {
                            JSONObject env = (JSONObject) o;
                            if (env.getInt("systemId") == systemId) {
                                environmentId = env.getInt("id");
                                break;
                            }
                        }
                    }
                } else {
                    JSONObject env = environments.getEnvironment(environmentId);
                    if (env.getInt("systemId") != systemId) {
                        return m;
                    }
                }
                JSONObject instances = environments.getEnvironmentInstances(environmentId);
                if (instances.has("instances")) {
                    JSONArray instArray = instances.getJSONArray("instances");
                    for (Object o : instArray) {
                        JSONObject inst = (JSONObject) o;
                        m.add(inst.getString("name"), inst.getString("id"));
                    }
                }
            } catch (IOException e) {
            }
            return m;
        }

        public ListBoxModel doFillServerIdItems() {
            ListBoxModel m = new ListBoxModel();
            try {
                EnvironmentManagerPluginDescriptor pluginDescriptor =
                    EnvironmentManagerPlugin.getEnvironmentManagerPluginDescriptor();
                String emUrl = pluginDescriptor.getEmUrl();
                String username = pluginDescriptor.getUsername();
                Secret password = pluginDescriptor.getPassword();
                if (emUrl != null) {
                    Servers servers = new ServersImpl(emUrl, username, password.getPlainText());
                    JSONObject response = servers.getServers();
                    if (response.has("servers")) {
                        JSONArray envArray = response.getJSONArray("servers");
                        for (Object o : envArray) {
                            JSONObject server = (JSONObject) o;
                            String name = server.getString("name");
                            String host = server.getString("host");
                            if (!name.equals(host)) {
                                name += " (" + host + ':' + server.getInt("port") + ')';
                            }
                            m.add(name, server.getString("id"));
                        }
                    }
                }
            } catch (IOException e) {
                e.printStackTrace();
            }
            return m;
        }
    }
}
```
