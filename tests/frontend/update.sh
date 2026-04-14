#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	update_error \
	file_url \
	update_disabled_repo \

update_error_body() {

	mkdir repos
	mkdir empty
	cat > repos/test.conf << EOF
test: {
  url: "file://empty/",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file://empty//meta.conf'" \
		-s exit:1 \
		pkg -R repos update
}

file_url_body() {
	mkdir repos
	touch meta.conf
	here=$(pwd)


#
# test file:/empty/, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:/empty/",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file:/empty//meta.conf" \
		-s exit:1 \
		pkg -R repos update

#
# test file://here, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://here",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file://here/meta.conf'" \
		-s exit:1 \
		pkg -R repos update


#
# test file://here//path, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://here/${here}",
}
EOF
	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file:////path, which is valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:///${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file:///path, which is valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

#
# test file://path, which is invalid
#
	cat > repos/test.conf << EOF
test: {
  url: "file:/${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e match:"Invalid URL: 'file:/${here}/meta.conf'" \
		-s exit:1 \
		pkg -R repos update


#
# test file://localhost/path, which is a valid
#
	cat > repos/test.conf << EOF
test: {
  url: "file://localhost${here}",
}
EOF

	atf_check \
		-o match:"Unable to update repository test" \
		-e not-match:"meta.*No such file or directory" \
		-s exit:1 \
		pkg -R repos update

}

# Verify that "pkg update -r <repo>" works on a disabled repository,
# as documented in the man page ("irrespective of the configured active
# status").
update_disabled_repo_body()
{
	# Create a package and repo
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	atf_check pkg create -M test.ucl
	atf_check -o ignore pkg repo .

	# Configure two repos: one enabled, one disabled
	mkdir repos
	cat <<EOF > repos/enabled.conf
enabled_repo: {
	url: "file://${TMPDIR}",
	enabled: yes
}
EOF
	cat <<EOF > repos/disabled.conf
disabled_repo: {
	url: "file://${TMPDIR}",
	enabled: no
}
EOF

	atf_check \
		-o match:"Updating enabled_repo" \
		-o not-match:"disabled_repo" \
		-s exit:0 \
		pkg -R repos update

	atf_check \
		-o match:"Updating disabled_repo" \
		-o not-match:"enabled_repo" \
		-s exit:0 \
		pkg -R repos update -r disabled_repo

	atf_check \
		-o match:"No repositories are enabled" \
		-s exit:1 \
		pkg -R repos update -r nonexistent
}
