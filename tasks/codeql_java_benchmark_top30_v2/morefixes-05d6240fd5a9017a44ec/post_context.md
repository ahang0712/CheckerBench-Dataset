# Patch-overlapping Java context after the fix

## `src/main/java/org/jenkinsci/plugins/DependencyTrack/DescriptorImpl.java` (changed lines (49, 153, 174, 196, 199, 200, 201, 202, 203, 204, 205, 213, 216, 217, 218, 219, 220, 221, 222, 237, 239, 240, 241, 242, 243, 248))

```java
/*
 * Copyright 2020 OWASP.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.jenkinsci.plugins.DependencyTrack;

import com.cloudbees.plugins.credentials.CredentialsProvider;
import com.cloudbees.plugins.credentials.common.StandardListBoxModel;
import edu.umd.cs.findbugs.annotations.CheckForNull;
import edu.umd.cs.findbugs.annotations.Nullable;
import hudson.Extension;
import hudson.model.AbstractProject;
import hudson.model.Descriptor;
import hudson.model.Item;
import hudson.security.ACL;
import hudson.tasks.BuildStepDescriptor;
import hudson.tasks.Publisher;
import hudson.util.FormValidation;
import hudson.util.ListBoxModel;
import hudson.util.Secret;
import java.io.Serializable;
import java.util.Collections;
import java.util.Comparator;
import java.util.Optional;
import java.util.stream.Collectors;
import jenkins.model.Jenkins;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;
import net.sf.json.JSONObject;
import org.apache.commons.lang.StringUtils;
import org.jenkinsci.Symbol;
import org.jenkinsci.plugins.plaincredentials.StringCredentials;
import org.kohsuke.stapler.AncestorInPath;
import org.kohsuke.stapler.DataBoundSetter;
import org.kohsuke.stapler.QueryParameter;
import org.kohsuke.stapler.StaplerRequest;
import org.kohsuke.stapler.verb.POST;

/**
 * <p>
 * Descriptor for {@link DependencyTrackPublisher}. Used as a singleton. The
 * class is marked as public so that it can be accessed from views.
 * <p>
 * See
 * <code>src/main/resources/org/jenkinsci/plugins/DependencyCheck/DependencyTrackBuilder/*.jelly</code>
 * for the actual HTML fragment for the configuration screen.
 */
@Extension
@Symbol("dependencyTrackPublisher") // This indicates to Jenkins that this is an implementation of an extension point.
public final class DescriptorImpl extends BuildStepDescriptor<Publisher> implements Serializable {

    private static final long serialVersionUID = -2018722914973282748L;

    private transient final ApiClientFactory clientFactory;

    /**
     * Specifies the base URL to Dependency-Track v3 or higher.
     */
    @Setter(onMethod_ = {@DataBoundSetter})
    private String dependencyTrackUrl;

    /**
     * Specifies the alternative base URL to the frontend of Dependency-Track v3 or higher.
     */
    @Setter(onMethod_ = {@DataBoundSetter})
    private String dependencyTrackFrontendUrl;

    /**
     * Specifies an API Key used for authentication (if authentication is
     * required).
     */
    @Getter(onMethod_ = {@CheckForNull})
    @Setter(onMethod_ = {@DataBoundSetter})
    private String dependencyTrackApiKey;

    /**
     * Specifies whether the API key provided has the PROJECT_CREATION_UPLOAD
     * permission.
     */
    @Getter
    @Setter(onMethod_ = {@DataBoundSetter})
    private boolean dependencyTrackAutoCreateProjects;

    /**
     * Specifies the maximum number of minutes to wait for synchronous jobs to
     * complete.
     */
    @Setter(onMethod_ = {@DataBoundSetter})
    private int dependencyTrackPollingTimeout;

    /**
     * Defines the number of seconds to wait between two checks for
     * Dependency-Track to process a job (Synchronous Publishing Mode).
     */
    @Setter(onMethod_ = {@DataBoundSetter})
    private int dependencyTrackPollingInterval;

    /**
     * the connection-timeout in seconds for every call to DT
     */
    @Getter
    @Setter(onMethod_ = {@DataBoundSetter})
    private int dependencyTrackConnectionTimeout;

    /**
     * the read-timeout in seconds for every call to DT
     */
    @Getter
    @Setter(onMethod_ = {@DataBoundSetter})
    private int dependencyTrackReadTimeout;

    /**
     * Default constructor. Obtains the Descriptor used in
     * DependencyCheckBuilder as this contains the global Dependency-Check
     * Jenkins plugin configuration.
     */
    public DescriptorImpl() {
        this(ApiClient::new);
    }

    DescriptorImpl(@NonNull ApiClientFactory clientFactory) {
        super(DependencyTrackPublisher.class);
        this.clientFactory = clientFactory;
        load();
    }

    @Override
    public boolean isApplicable(Class<? extends AbstractProject> aClass) {
        // Indicates that this builder can be used with all kinds of project types
        return true;
    }

    /**
     * Retrieve the projects to populate the dropdown.
     *
     * @param dependencyTrackUrl the base URL to Dependency-Track
     * @param dependencyTrackApiKey the API key to use for authentication
     * @param item used to lookup credentials in job config. ignored in global
     * @return ListBoxModel
     */
    @POST
    public ListBoxModel doFillProjectIdItems(@QueryParameter final String dependencyTrackUrl, @QueryParameter final String dependencyTrackApiKey, @AncestorInPath @Nullable Item item) {
        final ListBoxModel projects = new ListBoxModel();
        try {
            // url may come from instance-config. if empty, then take it from global config (this)
            final String url = Optional.ofNullable(PluginUtil.parseBaseUrl(dependencyTrackUrl)).orElse(getDependencyTrackUrl());
            // api-key may come from instance-config. if empty, then take it from global config (this)
            final String apiKey = lookupApiKey(Optional.ofNullable(StringUtils.trimToNull(dependencyTrackApiKey)).orElse(getDependencyTrackApiKey()), item);
            final ApiClient apiClient = getClient(url, apiKey);
            projects.addAll(apiClient.getProjects().stream()
                    .map(p -> new ListBoxModel.Option(p.getName().concat(" ").concat(Optional.ofNullable(p.getVersion()).orElse(StringUtils.EMPTY)).trim(), p.getUuid()))
                    .sorted(Comparator.comparing(o -> o.name))
                    .collect(Collectors.toList())
            );
            projects.add(0, new ListBoxModel.Option("-- Select Project --", null));
        } catch (ApiClientException e) {
            projects.add(Messages.Builder_Error_Projects(e.getLocalizedMessage()), null);
        }
        return projects;
    }

    @POST
    public ListBoxModel doFillDependencyTrackApiKeyItems(@QueryParameter String credentialsId, @AncestorInPath Item item) {
        StandardListBoxModel result = new StandardListBoxModel();
        if (item == null) {
            if (!Jenkins.get().hasPermission(Jenkins.ADMINISTER)) {
                return result.includeCurrentValue(credentialsId);
            }
        } else {
            if (!item.hasPermission(Item.EXTENDED_READ) && !item.hasPermission(CredentialsProvider.USE_ITEM)) {
                return result.includeCurrentValue(credentialsId);
            }
        }
        return result
                .includeEmptyValue()
                .includeAs(ACL.SYSTEM, item, StringCredentials.class, Collections.emptyList())
                .includeCurrentValue(credentialsId);
    }

    /**
     * Performs input validation when submitting the global config
     *
     * @param value The value of the URL as specified in the global config
     * @param item used to check permissions in job config. ignored in global
     * @return a FormValidation object
     */
    @POST
    public FormValidation doCheckDependencyTrackUrl(@QueryParameter String value, @AncestorInPath @Nullable Item item) {
        if (item == null) {
            Jenkins.get().checkPermission(Jenkins.ADMINISTER);
        } else {
            item.checkPermission(Item.CONFIGURE);
        }
        return PluginUtil.doCheckUrl(value);
    }

    /**
     * Performs input validation when submitting the global config
     *
     * @param value The value of the URL as specified in the global config
     * @param item used to check permissions in job config. ignored in global
     * @return a FormValidation object
     */
    @POST
    public FormValidation doCheckDependencyTrackFrontendUrl(@QueryParameter String value, @AncestorInPath @Nullable Item item) {
        if (item == null) {
            Jenkins.get().checkPermission(Jenkins.ADMINISTER);
        } else {
            item.checkPermission(Item.CONFIGURE);
        }
        return PluginUtil.doCheckUrl(value);
    }

    /**
     * Performs an on-the-fly check of the Dependency-Track URL and api key
     * parameters by making a simple call to the server and validating the
     * response code.
     *
     * @param dependencyTrackUrl the base URL to Dependency-Track
     * @param dependencyTrackApiKey the credential-id of the API key to use for authentication
     * @param item used to lookup credentials in job config. ignored in global
     * config
     * @return FormValidation
     */
    @POST
    public FormValidation doTestConnection(@QueryParameter final String dependencyTrackUrl, @QueryParameter final String dependencyTrackApiKey, @AncestorInPath @Nullable Item item) {
        if (item == null) {
            Jenkins.get().checkPermission(Jenkins.ADMINISTER);
        } else {
            item.checkPermission(Item.CONFIGURE);
        }
        // url may come from instance-config. if empty, then take it from global config (this)
        final String url = Optional.ofNullable(PluginUtil.parseBaseUrl(dependencyTrackUrl)).orElse(getDependencyTrackUrl());
        // api-key may come from instance-config. if empty, then take it from global config (this)
        final String apiKey = lookupApiKey(Optional.ofNullable(StringUtils.trimToNull(dependencyTrackApiKey)).orElse(getDependencyTrackApiKey()), item);
        if (doCheckDependencyTrackUrl(url, item).kind == FormValidation.Kind.OK && StringUtils.isNotBlank(apiKey)) {
            try {
                final ApiClient apiClient = getClient(url, apiKey);
                final String result = apiClient.testConnection();
                return result.startsWith("Dependency-Track v") ? FormValidation.ok("Connection successful - " + result) : FormValidation.error("Connection failed - " + result);
            } catch (ApiClientException e) {
                return FormValidation.error(e, "Connection failed");
            }
        }
        return FormValidation.warning("URL must be valid and Api-Key must not be empty");
    }

    /**
     * Takes the /apply/save step in the global config and saves the JSON data.
     *
     * @param req the request
     * @param formData the form data
     * @return a boolean
     * @throws FormException an exception validating form input
     */
    @Override
    public boolean configure(StaplerRequest req, JSONObject formData) throws Descriptor.FormException {
        req.bindJSON(this, formData);
        save();
        return super.configure(req, formData);
    }

    /**
     * This name is used on the build configuration screen.
     *
     * @return
     */
    @Override
    public String getDisplayName() {
        return Messages.Publisher_DependencyTrack_Name();
    }

    /**
     * @return global configuration for dependencyTrackUrl
     */
    @CheckForNull
    public String getDependencyTrackUrl() {
        return PluginUtil.parseBaseUrl(dependencyTrackUrl);
    }

    /**
     * @return global configuration for dependencyTrackFrontendUrl
     */
    @CheckForNull
    public String getDependencyTrackFrontendUrl() {
        return PluginUtil.parseBaseUrl(dependencyTrackFrontendUrl);
    }

    /**
     * @return global configuration for dependencyTrackPollingTimeout.
     */
    public int getDependencyTrackPollingTimeout() {
        if (dependencyTrackPollingTimeout <= 0) {
            return 5;
        }
        return dependencyTrackPollingTimeout;
    }

    /**
     * @return global configuration for dependencyTrackPollingInterval.
     */
    public int getDependencyTrackPollingInterval() {
        if (dependencyTrackPollingInterval <= 0) {
            return 10;
        }
        return dependencyTrackPollingInterval;
    }

    private ApiClient getClient(final String baseUrl, final String apiKey) {
        return clientFactory.create(baseUrl, apiKey, new ConsoleLogger(), Math.max(dependencyTrackConnectionTimeout, 0), Math.max(dependencyTrackReadTimeout, 0));
    }

    private String lookupApiKey(final String credentialId, final Item item) {
        return CredentialsProvider.lookupCredentials(StringCredentials.class, item, ACL.SYSTEM, Collections.emptyList()).stream()
                .filter(c -> c.getId().equals(credentialId))
                .map(StringCredentials::getSecret)
                .map(Secret::getPlainText)
                .findFirst().orElse(StringUtils.EMPTY);
    }
}
```

## `src/test/java/org/jenkinsci/plugins/DependencyTrack/DescriptorImplTest.java` (changed lines (165, 166, 167, 168))

```java
/*
 * Copyright 2020 OWASP.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.jenkinsci.plugins.DependencyTrack;

import com.cloudbees.plugins.credentials.CredentialsProvider;
import com.cloudbees.plugins.credentials.CredentialsScope;
import com.cloudbees.plugins.credentials.domains.Domain;
import hudson.model.Descriptor;
import hudson.util.FormValidation;
import hudson.util.ListBoxModel;
import hudson.util.Secret;
import io.jenkins.plugins.casc.misc.JenkinsConfiguredRule;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import net.sf.json.JSONObject;
import org.jenkinsci.plugins.DependencyTrack.model.Project;
import org.jenkinsci.plugins.plaincredentials.impl.StringCredentialsImpl;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.kohsuke.stapler.StaplerRequest;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.mockito.quality.Strictness;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

/**
 *
 * @author Ronny "Sephiroth" Perinke <sephiroth@sephiroth-j.de>
 */
public class DescriptorImplTest {

    @Rule
    public JenkinsConfiguredRule r = new JenkinsConfiguredRule();

    @Rule
    public MockitoRule rule = MockitoJUnit.rule().strictness(Strictness.STRICT_STUBS);

    @Mock
    private ApiClient client;
    private DescriptorImpl uut;

    @Before
    public void setup() {
        uut = new DescriptorImpl((url, apiKey, logger, connTimeout, readTimeout) -> client);
    }

    @Test
    public void doFillProjectIdItemsTest() throws ApiClientException {
        List<Project> projects = new ArrayList<>();
        projects.add(Project.builder().name("Project 2").uuid("uuid-2").version("1.2.3").build());
        projects.add(Project.builder().name("Project 1").uuid("uuid-1").build());
        doReturn(projects).doThrow(new ApiClientException("test failure"))
                .when(client).getProjects();

        assertThat(uut.doFillProjectIdItems(null, null, null)).usingElementComparatorOnFields("name", "value", "selected").containsExactly(
                new ListBoxModel.Option("-- Select Project --", null, false),
                new ListBoxModel.Option("Project 1", "uuid-1", false),
                new ListBoxModel.Option("Project 2 1.2.3", "uuid-2", false)
        );

        assertThat(uut.doFillProjectIdItems(null, null, null)).usingElementComparatorOnFields("name", "value", "selected").containsExactly(
                new ListBoxModel.Option(Messages.Builder_Error_Projects("test failure"), null, false)
        );
    }

    @Test
    public void doFillDependencyTrackApiKeyItems() throws IOException {
        final String apikey = "api-key";
        final String credentialsid = "credentials-id";
        CredentialsProvider.lookupStores(r.jenkins).iterator().next().addCredentials(Domain.global(), new StringCredentialsImpl(CredentialsScope.GLOBAL, credentialsid, "test", Secret.fromString(apikey)));
        assertThat(uut.doFillDependencyTrackApiKeyItems(null, null)).usingElementComparatorOnFields("name", "value", "selected").containsExactly(
                new ListBoxModel.Option("- none -", "", false),
                new ListBoxModel.Option("test", credentialsid, false)
        );
        assertThat(uut.doFillDependencyTrackApiKeyItems(credentialsid, null)).usingElementComparatorOnFields("name", "value", "selected").containsExactly(
                new ListBoxModel.Option("- none -", "", false),
                new ListBoxModel.Option("test", credentialsid, false)
        );
    }

    @Test
    public void doTestConnectionTest() throws ApiClientException, IOException {
        final String apikey = "api-key";
        final String credentialsid = "credentials-id";
        // custom factory here so we can check that doTestConnection strips trailing slashes from the url
        ApiClientFactory factory = (url, apiKey, logger, connTimeout, readTimeout) -> {
            assertThat(url).isEqualTo("http:///url.tld");
            assertThat(apiKey).isEqualTo(apikey);
            assertThat(logger).isInstanceOf(ConsoleLogger.class);
            return client;
        };
        when(client.testConnection()).thenReturn("Dependency-Track v3.8.0").thenReturn("test").thenThrow(ApiClientException.class);
        CredentialsProvider.lookupStores(r.jenkins).iterator().next().addCredentials(Domain.global(), new StringCredentialsImpl(CredentialsScope.GLOBAL, credentialsid, "test", Secret.fromString(apikey)));
        uut = new DescriptorImpl(factory);

        assertThat(uut.doTestConnection("http:///url.tld", credentialsid, null))
                .hasFieldOrPropertyWithValue("kind", FormValidation.Kind.OK)
                .hasMessage("Connection successful - Dependency-Track v3.8.0")
                .hasNoCause();

        assertThat(uut.doTestConnection("http:///url.tld/", credentialsid, null))
                .hasFieldOrPropertyWithValue("kind", FormValidation.Kind.ERROR)
                .hasMessageStartingWith("Connection failed - test")
                .hasNoCause();

        assertThat(uut.doTestConnection("http:///url.tld/", credentialsid, null))
                .hasFieldOrPropertyWithValue("kind", FormValidation.Kind.ERROR)
                .hasMessageStartingWith("Connection failed")
                .hasMessageContaining(ApiClientException.class.getCanonicalName())
                .hasNoCause();

        assertThat(uut.doTestConnection("url", "", null))
                .hasFieldOrPropertyWithValue("kind", FormValidation.Kind.WARNING)
                .hasMessage("URL must be valid and Api-Key must not be empty")
                .hasNoCause();
    }

    @Test
    public void doTestConnectionTestWithEmptyArgs() throws ApiClientException, IOException {
        final String apikey = "api-key";
        final String credentialsid = "credentials-id";
        // custom factory here so we can check that doTestConnection strips trailing slashes from the url
        ApiClientFactory factory = (url, apiKey, logger, connTimeout, readTimeout) -> {
            assertThat(url).isEqualTo("http:///url.tld");
            assertThat(apiKey).isEqualTo(apikey);
            assertThat(logger).isInstanceOf(ConsoleLogger.class);
            return client;
        };
        when(client.testConnection()).thenReturn("Dependency-Track v3.8.0").thenReturn("test").thenThrow(ApiClientException.class);
        CredentialsProvider.lookupStores(r.jenkins).iterator().next().addCredentials(Domain.global(), new StringCredentialsImpl(CredentialsScope.GLOBAL, credentialsid, "test", Secret.fromString(apikey)));
        uut = new DescriptorImpl(factory);
        uut.setDependencyTrackApiKey(credentialsid);
        uut.setDependencyTrackUrl("http:///url.tld/");

        assertThat(uut.doTestConnection("", "", null))
                .hasFieldOrPropertyWithValue("kind", FormValidation.Kind.OK)
                .hasMessage("Connection successful - Dependency-Track v3.8.0")
                .hasNoCause();
    }

    @Test
    public void doCheckDependencyTrackUrlTest() {
        assertThat(uut.doCheckDependencyTrackUrl("http://foo.bar/", null)).isEqualTo(FormValidation.ok());
        assertThat(uut.doCheckDependencyTrackUrl("http://foo.bar", null)).isEqualTo(FormValidation.ok());
        assertThat(uut.doCheckDependencyTrackUrl("", null)).isEqualTo(FormValidation.ok());
        assertThat(uut.doCheckDependencyTrackUrl("foo", null))
                .hasFieldOrPropertyWithValue("kind", FormValidation.Kind.ERROR)
                .hasMessage("The specified value is not a valid URL");
    }

    @Test
    public void getDependencyTrackUrlTest() {
        uut.setDependencyTrackUrl("http://foo.bar/");
        assertThat(uut.getDependencyTrackUrl()).isEqualTo("http://foo.bar");

        uut.setDependencyTrackUrl("http://foo.bar");
        assertThat(uut.getDependencyTrackUrl()).isEqualTo("http://foo.bar");
    }

    @Test
    public void getDependencyTrackPollingTimeoutTest() {
        assertThat(uut.getDependencyTrackPollingTimeout()).isEqualTo(5);

        uut.setDependencyTrackPollingTimeout(0);
        assertThat(uut.getDependencyTrackPollingTimeout()).isEqualTo(5);

        uut.setDependencyTrackPollingTimeout(Integer.MAX_VALUE);
        assertThat(uut.getDependencyTrackPollingTimeout()).isEqualTo(Integer.MAX_VALUE);
    }

    @Test
    public void configureTest() throws Descriptor.FormException {
        StaplerRequest req = mock(StaplerRequest.class);
        JSONObject formData = new JSONObject()
                .element("dependencyTrackUrl", "https://foo.bar/")
                .element("dependencyTrackApiKey", "api-key")
                .element("dependencyTrackAutoCreateProjects", true)
                .element("dependencyTrackPollingTimeout", 7);

        assertThat(uut.configure(req, formData)).isTrue();

        verify(req).bindJSON(eq(uut), eq(formData));
    }
}
```
