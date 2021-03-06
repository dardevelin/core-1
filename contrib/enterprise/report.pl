#!/usr/bin/perl

# originally by Shane McEwan <shane@mcewan.id.au>
# maintained by Ted Zlatanov <tzz@lifelogs.com>

use warnings;
use strict;
use LWP::UserAgent;
use JSON;
use Data::Dumper;
use Getopt::Long;

my %outputs = (
               csv => sub { my $data = decode_json( shift ); print_csv($data) },
               html => sub { my $data = decode_json( shift ); print_html($data) },
               json => sub { print @_ },
              );

my %options = (
               url => 'http://admin:admin@localhost:80/',
               output => 'csv',
               limit => 100000,
               verbose => 0,
              );

GetOptions(\%options,
           "verbose|v!",
           "output:s",
           "limit:i",           # note that you can say "LIMIT N" in the query!
           "url:s",
          );

# Create a user agent object
my $ua = LWP::UserAgent->new;

my $query = shift;

die "Syntax: $0 [--url BASEURL] [--limit N] [--output @{[ join('|', sort keys %outputs) ]}] QUERY"
 unless ($query && exists $outputs{$options{output}});

$query =~ s/\v+/ /g;

# Create a request
my $url = "$options{url}/api/query";
my $req = HTTP::Request->new(POST => $url);
$req->content_type('application/x-www-form-urlencoded');
$req->content(sprintf('{ "limit": %d, "query" : "%s" }',
                      $options{limit},
                      $query));

print "Opening $url...\n" if $options{verbose};
print "Query is [$query]\n" if $options{verbose};

# Pass request to the user agent and get a response back
my $res = $ua->request($req);

unless ($res->is_success)
{
 die $res->status_line;
}

print "Got data ", $res->content(), "\n" if $options{verbose};
$outputs{$options{output}}->($res->content);

exit 0;

sub print_csv
{
 my $data = shift;

 foreach my $row (@{$data->{data}->[0]->{rows}})
 {
  print join ",", map { m/,/ ? "\"$_\"" : $_ } @$row;
  print "\n";
 }
}

sub print_html
{
 my $data = shift;

 # trying to avoid requiring CPAN modules!  sorry this is so primitive!
 print "<html><body><table>\n";
 foreach my $row (@{$data->{data}->[0]->{rows}})
 {
  print "<tr>\n";
  foreach my $item (@$row)
  {
   print "<td>\n";
   print "$item\n";             # should be escaped for proper output
   print "</td>\n";
  }
  print "</tr>\n";
 }
 print "</table></body></html>\n";
}
